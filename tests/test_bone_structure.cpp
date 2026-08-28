// ═════════════════════════════════════════════════════════════════════════════
// test_bone_structure — logica pura del panel de estructura de huesos
//
// El dibujado con ImGui no se puede testear headless, asi que BoneStructurePanel
// deja fuera de la UI todo lo que decide algo: el armado del arbol, la
// validacion, el reparentado seguro con su reordenamiento topologico y el
// remapeo de indices que mantiene vivo al .dashmesh.
// ═════════════════════════════════════════════════════════════════════════════
#include "panels/BoneStructurePanel.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace bs = dash::editor::bonestruct;
namespace fs = std::filesystem;

static int g_pass = 0, g_fail = 0;

#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::fprintf(stderr, "  FAIL [%s:%d] %s\n", __FILE__, __LINE__, msg); \
        ++g_fail; \
    } else { ++g_pass; } \
} while(0)

namespace {

fs::path tempDir()
{
    static const fs::path dir = fs::temp_directory_path() / "dash_test_bone_structure";
    fs::create_directories(dir);
    return dir;
}

bs::Bone makeBone(const std::string& name, int parent, float tx = 0.f)
{
    bs::Bone b;
    b.name         = name;
    b.parent       = parent;
    b.localBind    = dash::anim::identity();
    b.offsetMatrix = dash::anim::identity();
    b.localBind.m[12] = tx;
    return b;
}

// Root
//  +- Spine
//  |   +- Head
//  |   +- ArmL
//  +- Hips
std::vector<bs::Bone> makeRig()
{
    return {
        makeBone("Root",  -1, 0.f),
        makeBone("Spine",  0, 1.f),
        makeBone("Head",   1, 2.f),
        makeBone("ArmL",   1, 3.f),
        makeBone("Hips",   0, 4.f),
    };
}

int indexOf(const std::vector<bs::Bone>& bones, const std::string& name)
{
    for (int i = 0; i < static_cast<int>(bones.size()); ++i)
        if (bones[static_cast<std::size_t>(i)].name == name) return i;
    return -1;
}

bool parentBeforeChild(const std::vector<bs::Bone>& bones)
{
    for (std::size_t i = 0; i < bones.size(); ++i)
        if (bones[i].parent >= static_cast<int>(i)) return false;
    return true;
}

// Reconstruye la relacion padre/hijo por NOMBRE, que es lo unico que sobrevive
// a un reordenamiento: si el remapeo esta bien, la jerarquia nombrada no cambia.
std::vector<std::pair<std::string, std::string>> namedEdges(const std::vector<bs::Bone>& bones)
{
    std::vector<std::pair<std::string, std::string>> out;
    for (const bs::Bone& b : bones) {
        const std::string parent =
            b.parent < 0 ? std::string("(root)") : bones[static_cast<std::size_t>(b.parent)].name;
        out.emplace_back(b.name, parent);
    }
    return out;
}

bool sameEdges(std::vector<std::pair<std::string, std::string>> a,
               std::vector<std::pair<std::string, std::string>> b)
{
    if (a.size() != b.size()) return false;
    std::sort(a.begin(), a.end());
    std::sort(b.begin(), b.end());
    return a == b;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
void test_hierarchy_from_flat_vector()
{
    std::printf("\n[test_hierarchy_from_flat_vector]\n");

    const std::vector<bs::Bone> rig = makeRig();
    const bs::Hierarchy         h   = bs::buildHierarchy(rig);

    ASSERT(h.roots.size() == 1 && h.roots[0] == 0, "detecta un unico root");
    ASSERT(h.children[0].size() == 2, "Root tiene dos hijos");
    ASSERT(h.children[0][0] == 1 && h.children[0][1] == 4, "los hijos van en orden de indice");
    ASSERT(h.children[1].size() == 2, "Spine tiene dos hijos");
    ASSERT(h.children[2].empty(), "Head es hoja");

    ASSERT(bs::depthOf(rig, 0) == 0, "el root esta a profundidad 0");
    ASSERT(bs::depthOf(rig, 2) == 2, "Head esta a profundidad 2");

    ASSERT(bs::isDescendantOf(rig, 2, 0), "Head desciende de Root");
    ASSERT(bs::isDescendantOf(rig, 2, 1), "Head desciende de Spine");
    ASSERT(!bs::isDescendantOf(rig, 4, 1), "Hips no desciende de Spine");
    ASSERT(bs::isDescendantOf(rig, 1, 1), "un hueso es descendiente de si mismo");

    // Un padre fuera de rango no puede tragarse la rama: aparece como root.
    std::vector<bs::Bone> broken = rig;
    broken[3].parent = 99;
    const bs::Hierarchy bh = bs::buildHierarchy(broken);
    ASSERT(bh.roots.size() == 2, "el hueso con padre invalido se expone como root");

    const std::vector<bool> reachable = bs::reachableFromRoots(rig);
    for (bool r : reachable) ASSERT(r, "todo hueso de un rig sano llega al root");
}

// ─────────────────────────────────────────────────────────────────────────────
void test_validation_reports_every_defect()
{
    std::printf("\n[test_validation_reports_every_defect]\n");

    ASSERT(bs::validate(makeRig()).empty(), "un rig sano no reporta nada");

    // Ciclo: Spine <-> Head, ninguno de los dos llega a un root.
    std::vector<bs::Bone> cyclic = makeRig();
    cyclic[1].parent = 2;
    const std::vector<bool> reachable = bs::reachableFromRoots(cyclic);
    ASSERT(!reachable[1] && !reachable[2], "los huesos del ciclo no llegan al root");
    ASSERT(reachable[0] && reachable[4], "el resto del rig sigue siendo alcanzable");
    ASSERT(bs::hasErrors(bs::validate(cyclic)), "el ciclo es un error");
    ASSERT(!bs::topologicalSort(cyclic).ok, "no se puede ordenar una jerarquia ciclica");
    ASSERT(!bs::topologicalSort(cyclic).error.empty(), "el fallo trae mensaje");

    std::vector<bs::Bone> outOfRange = makeRig();
    outOfRange[3].parent = 42;
    ASSERT(bs::hasErrors(bs::validate(outOfRange)), "padre fuera de rango es un error");

    std::vector<bs::Bone> selfParent = makeRig();
    selfParent[3].parent = 3;
    ASSERT(bs::hasErrors(bs::validate(selfParent)), "auto-padre es un error");

    std::vector<bs::Bone> duplicated = makeRig();
    duplicated[4].name = "Head";
    ASSERT(bs::hasErrors(bs::validate(duplicated)), "nombres duplicados son un error");

    std::vector<bs::Bone> empty = makeRig();
    empty[2].name.clear();
    ASSERT(bs::hasErrors(bs::validate(empty)), "nombre vacio es un error");

    // Invariante topologico roto: el padre queda con indice mayor que el hijo.
    std::vector<bs::Bone> unsorted = {
        makeBone("Head", 1), makeBone("Spine", 2), makeBone("Root", -1)
    };
    ASSERT(!parentBeforeChild(unsorted), "el fixture arranca desordenado");
    ASSERT(bs::hasErrors(bs::validate(unsorted)), "padre despues del hijo es un error");
    ASSERT(bs::reachableFromRoots(unsorted)[0], "desordenado no quiere decir ciclico");

    // El limite de 128 huesos del pipeline es aviso, no error: el archivo es valido.
    std::vector<bs::Bone> huge;
    huge.push_back(makeBone("b0", -1));
    for (int i = 1; i < 140; ++i) huge.push_back(makeBone("b" + std::to_string(i), 0));
    const std::vector<bs::Issue> issues = bs::validate(huge);
    ASSERT(!issues.empty() && !bs::hasErrors(issues), "pasarse del limite de GPU es warning");
}

// ─────────────────────────────────────────────────────────────────────────────
void test_topological_sort_keeps_the_hierarchy()
{
    std::printf("\n[test_topological_sort_keeps_the_hierarchy]\n");

    // Orden invertido a proposito: cada padre esta despues de su hijo.
    const std::vector<bs::Bone> unsorted = {
        makeBone("ArmL",  3, 3.f),
        makeBone("Head",  3, 2.f),
        makeBone("Hips",  4, 4.f),
        makeBone("Spine", 4, 1.f),
        makeBone("Root", -1, 0.f),
    };
    ASSERT(!parentBeforeChild(unsorted), "el fixture arranca con padre > hijo");

    const bs::ReorderResult r = bs::topologicalSort(unsorted);
    ASSERT(r.ok, "ordena un arbol valido aunque venga al reves");
    ASSERT(r.reordered, "reporta que los indices se movieron");
    ASSERT(r.bones.size() == unsorted.size(), "no pierde ni duplica huesos");
    ASSERT(parentBeforeChild(r.bones), "tras ordenar, todo padre tiene indice menor que el hijo");
    ASSERT(bs::validate(r.bones).empty(), "el resultado pasa la validacion");

    // La permutacion es biyectiva y apunta a los mismos huesos.
    std::vector<int> seen(r.oldToNew.size(), 0);
    for (std::size_t old = 0; old < r.oldToNew.size(); ++old) {
        const int mapped = r.oldToNew[old];
        ASSERT(mapped >= 0 && mapped < static_cast<int>(r.bones.size()),
               "cada indice viejo cae dentro del vector nuevo");
        ++seen[static_cast<std::size_t>(mapped)];
        ASSERT(r.bones[static_cast<std::size_t>(mapped)].name == unsorted[old].name,
               "el indice remapeado sigue apuntando al mismo hueso");
        ASSERT(r.bones[static_cast<std::size_t>(mapped)].localBind.m[12] ==
                   unsorted[old].localBind.m[12],
               "el remapeo se lleva las matrices del hueso, no solo el nombre");
    }
    for (int count : seen) ASSERT(count == 1, "ningun indice nuevo recibe dos huesos");

    ASSERT(sameEdges(namedEdges(unsorted), namedEdges(r.bones)),
           "la relacion padre/hijo por nombre sobrevive intacta al reordenamiento");

    ASSERT(indexOf(r.bones, "Root") < indexOf(r.bones, "Spine"), "Root queda antes que Spine");
    ASSERT(indexOf(r.bones, "Spine") < indexOf(r.bones, "Head"), "Spine queda antes que Head");

    // Un rig que ya cumple el invariante no se toca.
    const bs::ReorderResult noop = bs::topologicalSort(makeRig());
    ASSERT(noop.ok && !noop.reordered, "un rig ya ordenado no se reordena");
    ASSERT(bs::isIdentityPermutation(noop.oldToNew), "la permutacion es la identidad");

    // Un padre roto se normaliza a root en vez de arrastrar basura.
    std::vector<bs::Bone> broken = makeRig();
    broken[3].parent = 77;
    const bs::ReorderResult fixed = bs::topologicalSort(broken);
    ASSERT(fixed.ok, "ordena igual con un padre fuera de rango");
    ASSERT(fixed.bones[static_cast<std::size_t>(fixed.oldToNew[3])].parent == -1,
           "el padre invalido se normaliza a -1");
    ASSERT(bs::validate(fixed.bones).empty(), "y el resultado queda limpio");
}

// ─────────────────────────────────────────────────────────────────────────────
void test_reparent_valid_and_invalid()
{
    std::printf("\n[test_reparent_valid_and_invalid]\n");

    const std::vector<bs::Bone> rig = makeRig();

    // Invalido: mover Root bajo Head, que es su propio descendiente.
    const bs::ReorderResult cycle = bs::reparent(rig, 0, 2);
    ASSERT(!cycle.ok, "rechaza reparentar un hueso bajo su descendiente");
    ASSERT(!cycle.error.empty(), "explica por que lo rechaza");
    ASSERT(cycle.bones.empty(), "no devuelve un vector a medio construir");

    ASSERT(!bs::reparent(rig, 1, 1).ok, "rechaza que un hueso sea su propio padre");
    ASSERT(!bs::reparent(rig, 9, 0).ok, "rechaza un hueso fuera de rango");
    ASSERT(!bs::reparent(rig, 0, 9).ok, "rechaza un padre fuera de rango");

    // Valido: Head pasa de colgar de Spine a colgar de Hips (indice mayor),
    // asi que el reordenamiento es obligatorio.
    const bs::ReorderResult moved = bs::reparent(rig, 2, 4);
    ASSERT(moved.ok, "acepta un reparentado que no cierra ciclos");
    ASSERT(moved.reordered, "mover bajo un hueso posterior obliga a reordenar");
    ASSERT(parentBeforeChild(moved.bones), "el invariante padre<hijo se mantiene");
    ASSERT(bs::validate(moved.bones).empty(), "el resultado pasa la validacion");

    const int newHead = moved.oldToNew[2];
    const int newHips = moved.oldToNew[4];
    ASSERT(moved.bones[static_cast<std::size_t>(newHead)].name == "Head",
           "el indice remapeado de Head apunta a Head");
    ASSERT(moved.bones[static_cast<std::size_t>(newHead)].parent == newHips,
           "Head cuelga del indice nuevo de Hips");
    ASSERT(newHips < newHead, "el padre nuevo quedo antes que el hijo");
    ASSERT(bs::isDescendantOf(moved.bones, newHead, newHips), "y la jerarquia lo refleja");
    ASSERT(moved.bones.size() == rig.size(), "no se pierde ningun hueso en el camino");

    // Sacarlo a root tambien es valido; como los roots se recorren en orden,
    // ArmL se va al final del vector y los indices se corren.
    const bs::ReorderResult toRoot = bs::reparent(rig, 3, -1);
    ASSERT(toRoot.ok, "acepta convertir un hueso en root");
    const int newArm = toRoot.oldToNew[3];
    ASSERT(toRoot.bones[static_cast<std::size_t>(newArm)].name == "ArmL",
           "el indice remapeado sigue siendo ArmL");
    ASSERT(toRoot.bones[static_cast<std::size_t>(newArm)].parent == -1, "ArmL quedo como root");
    ASSERT(parentBeforeChild(toRoot.bones), "y el invariante se mantiene");

    // Reparentados encadenados: la permutacion acumulada sigue siendo coherente.
    const bs::ReorderResult second = bs::reparent(moved.bones, moved.oldToNew[3], newHead);
    ASSERT(second.ok, "el segundo reparentado tambien es valido");
    const std::vector<int> total = bs::composePermutation(moved.oldToNew, second.oldToNew);
    for (std::size_t old = 0; old < rig.size(); ++old) {
        ASSERT(second.bones[static_cast<std::size_t>(total[old])].name == rig[old].name,
               "la permutacion compuesta sigue apuntando al mismo hueso original");
    }
    ASSERT(!sameEdges(namedEdges(moved.bones), namedEdges(second.bones)),
           "el segundo reparentado si cambio la jerarquia nombrada");
}

// ─────────────────────────────────────────────────────────────────────────────
void test_rename_and_filter()
{
    std::printf("\n[test_rename_and_filter]\n");

    std::vector<bs::Bone> rig = makeRig();

    ASSERT(!bs::renameBone(rig, 2, "").ok, "rechaza nombre vacio");
    ASSERT(!bs::renameBone(rig, 2, "   ").ok, "rechaza nombre de solo espacios");
    ASSERT(!bs::renameBone(rig, 2, "Hips").ok, "rechaza un nombre ya usado");
    ASSERT(!bs::renameBone(rig, 99, "X").ok, "rechaza un indice fuera de rango");
    ASSERT(rig[2].name == "Head", "un rename rechazado no toca el hueso");

    ASSERT(bs::renameBone(rig, 2, "  Skull  ").ok, "acepta un nombre libre");
    ASSERT(rig[2].name == "Skull", "recorta los espacios de los extremos");
    ASSERT(bs::renameBone(rig, 2, "Skull").ok, "renombrar al mismo nombre es valido");

    const std::vector<bool> all = bs::filterVisibility(rig, "");
    for (bool v : all) ASSERT(v, "filtro vacio muestra todo");

    // "arm" solo matchea ArmL, pero sus ancestros quedan visibles para no
    // romper el camino en el arbol.
    const std::vector<bool> armOnly = bs::filterVisibility(rig, "ARM");
    ASSERT(armOnly[3], "el hueso que matchea es visible");
    ASSERT(armOnly[1] && armOnly[0], "sus ancestros siguen visibles");
    ASSERT(!armOnly[2] && !armOnly[4], "el resto se oculta");

    ASSERT(bs::containsCaseInsensitive("Spine", "pin"), "el substring ignora mayusculas");
    ASSERT(!bs::containsCaseInsensitive("Spine", "spines"), "un needle mas largo no matchea");
}

// ─────────────────────────────────────────────────────────────────────────────
void test_trs_decomposition()
{
    std::printf("\n[test_trs_decomposition]\n");

    const dash::anim::Vec3 t{1.5f, -2.f, 3.25f};
    const dash::anim::Quat q =
        dash::anim::normalize(dash::anim::Quat{0.2f, 0.4f, 0.1f, 0.8f});
    const dash::anim::Vec3 s{2.f, 3.f, 0.5f};

    const bs::Trs got = bs::decomposeTrs(dash::anim::composeTRS(t, q, s));

    auto near = [](float a, float b) { return std::fabs(a - b) < 1e-3f; };
    ASSERT(near(got.translation.x, t.x) && near(got.translation.y, t.y) &&
               near(got.translation.z, t.z),
           "recupera la traslacion");
    ASSERT(near(got.scale.x, s.x) && near(got.scale.y, s.y) && near(got.scale.z, s.z),
           "recupera la escala");
    // El signo del cuaternion es ambiguo: q y -q son la misma rotacion.
    const float d = std::fabs(dash::anim::dot(got.rotation, q));
    ASSERT(near(d, 1.f), "recupera la rotacion salvo el signo");

    dash::anim::Mat4 m = dash::anim::identity();
    bs::setTranslation(m, dash::anim::Vec3{4.f, 5.f, 6.f});
    ASSERT(m.m[12] == 4.f && m.m[13] == 5.f && m.m[14] == 6.f,
           "setTranslation escribe la ultima columna");
}

// ─────────────────────────────────────────────────────────────────────────────
void test_skin_index_remap_survives_the_reorder()
{
    std::printf("\n[test_skin_index_remap_survives_the_reorder]\n");

    const std::vector<bs::Bone> unsorted = {
        makeBone("Head",  2), makeBone("Spine", 2), makeBone("Root", -1),
    };
    const bs::ReorderResult r = bs::topologicalSort(unsorted);
    ASSERT(r.ok && r.reordered, "el fixture necesita reordenarse");

    // Un vertice pegado a Head y otro a Root, con los indices del orden viejo.
    std::vector<dash::vkexp::SkinnedVertex> skin(2);
    skin[0].boneIndices = {{0, 1, 0, 0}};
    skin[0].boneWeights = {{0.7f, 0.3f, 0.f, 0.f}};
    skin[1].boneIndices = {{2, 2, 2, 2}};
    skin[1].boneWeights = {{1.f, 0.f, 0.f, 0.f}};

    std::string error;
    ASSERT(bs::remapSkinIndices(skin, r.oldToNew, error), "el remapeo del skin funciona");
    ASSERT(r.bones[skin[0].boneIndices[0]].name == "Head",
           "el vertice sigue pesando sobre Head despues del reordenamiento");
    ASSERT(r.bones[skin[0].boneIndices[1]].name == "Spine", "y su segunda influencia sobre Spine");
    ASSERT(r.bones[skin[1].boneIndices[0]].name == "Root", "el otro vertice sigue en Root");
    ASSERT(skin[0].boneWeights[0] == 0.7f, "los pesos no se tocan");

    std::vector<dash::vkexp::SkinnedVertex> bad(1);
    bad[0].boneIndices = {{9, 0, 0, 0}};
    ASSERT(!bs::remapSkinIndices(bad, r.oldToNew, error),
           "un indice fuera del esqueleto se reporta en vez de escribirse mal");
}

// ─────────────────────────────────────────────────────────────────────────────
void test_file_round_trip_and_sibling_fixups()
{
    std::printf("\n[test_file_round_trip_and_sibling_fixups]\n");

    const std::string skelPath = (tempDir() / "rig.dashskel").string();
    const std::string meshPath = (tempDir() / "rig.dashmesh").string();
    const std::string animPath = (tempDir() / "rig.dashanim").string();

    bs::SkeletonDoc doc;
    doc.bones = {makeBone("Head", 2), makeBone("Spine", 2), makeBone("Root", -1)};
    doc.globalInverse = dash::anim::identity();

    std::string error;
    ASSERT(bs::saveSkeletonDoc(skelPath, doc, error), "escribe el .dashskel");

    bs::SkeletonDoc loaded;
    ASSERT(bs::loadSkeletonDoc(skelPath, loaded, error), "lo vuelve a leer");
    ASSERT(loaded.bones.size() == 3, "conserva la cantidad de huesos");
    ASSERT(loaded.bones[0].name == "Head" && loaded.bones[0].parent == 2,
           "conserva nombre y padre tal cual, sin reordenar por su cuenta");

    // Un esqueleto con nombres duplicados no se guarda a medias.
    bs::SkeletonDoc dup = doc;
    dup.bones[1].name = "Head";
    ASSERT(!bs::saveSkeletonDoc((tempDir() / "dup.dashskel").string(), dup, error),
           "se niega a guardar nombres duplicados en vez de perder huesos");

    // Mesh skinneado contra el orden viejo.
    dash::anim::DashMeshData mesh;
    mesh.vertices.resize(2);
    mesh.skin.resize(2);
    mesh.skin[0].boneIndices = {{0, 0, 0, 0}};   // Head
    mesh.skin[1].boneIndices = {{2, 0, 0, 0}};   // Root
    mesh.indices   = {0, 1, 0};
    mesh.boneCount = 3;
    ASSERT(dash::anim::writeDashMesh(meshPath, mesh, error), "escribe el .dashmesh");

    const bs::ReorderResult sorted = bs::topologicalSort(loaded.bones);
    ASSERT(sorted.ok && sorted.reordered, "el esqueleto del archivo necesita reordenarse");

    ASSERT(bs::remapMeshBoneIndices(meshPath, sorted.oldToNew, error),
           "remapea el mesh contra la permutacion");
    dash::anim::DashMeshData reloaded;
    ASSERT(dash::anim::readDashMesh(meshPath, reloaded, error), "relee el mesh remapeado");
    ASSERT(sorted.bones[reloaded.skin[0].boneIndices[0]].name == "Head",
           "el vertice sigue apuntando a Head tras el round-trip por disco");
    ASSERT(sorted.bones[reloaded.skin[1].boneIndices[0]].name == "Root",
           "y el otro a Root");

    // Un mesh horneado contra otra cantidad de huesos no se toca.
    dash::anim::DashMeshData other = mesh;
    other.boneCount = 7;
    const std::string otherPath = (tempDir() / "other.dashmesh").string();
    ASSERT(dash::anim::writeDashMesh(otherPath, other, error), "escribe el mesh desalineado");
    ASSERT(!bs::remapMeshBoneIndices(otherPath, sorted.oldToNew, error),
           "detecta que el mesh no fue horneado contra este esqueleto");

    // Los canales de animacion referencian huesos por nombre.
    dash::anim::AnimationClip clip;
    clip.name           = "Idle";
    clip.duration       = 1.f;
    clip.ticksPerSecond = 30.f;
    dash::anim::AnimationChannel channel;
    channel.boneName = "Head";
    channel.positions.push_back({0.f, {0.f, 0.f, 0.f}});
    clip.channels.push_back(channel);
    ASSERT(dash::anim::writeAnimationClips(animPath, {clip}, error), "escribe el .dashanim");

    const std::vector<std::pair<std::string, std::string>> renames = {{"Head", "Skull"}};
    ASSERT(bs::renameAnimChannels(animPath, renames, error) == 1, "retargetea un canal");

    std::vector<dash::anim::AnimationClip> back;
    ASSERT(dash::anim::readAnimationClips(animPath, back, error), "relee los clips");
    ASSERT(back.size() == 1 && back[0].channels.size() == 1, "no pierde clips ni canales");
    ASSERT(back[0].channels[0].boneName == "Skull", "el canal quedo atado al nombre nuevo");
    ASSERT(bs::renameAnimChannels(animPath, renames, error) == 0,
           "un rename que ya no aplica no toca nada");
}

// ─────────────────────────────────────────────────────────────────────────────
void test_offsets_follow_the_bind_pose()
{
    std::printf("\n[test_offsets_follow_the_bind_pose]\n");

    bs::SkeletonDoc doc;
    doc.bones = {makeBone("Root", -1, 0.f), makeBone("Spine", 0, 1.f), makeBone("Head", 1, 2.f)};
    bs::recomputeOffsetsFromBindPose(doc);

    dash::anim::Skeleton skeleton;
    for (const bs::Bone& b : doc.bones)
        skeleton.addBone(b.name, b.parent, b.offsetMatrix, b.localBind);

    std::vector<dash::anim::Mat4> pose;
    skeleton.bindPoseMatrices(pose);

    const dash::anim::Mat4 id = dash::anim::identity();
    for (const dash::anim::Mat4& m : pose)
        for (int i = 0; i < 16; ++i)
            ASSERT(std::fabs(m.m[i] - id.m[i]) < 1e-4f,
                   "con los offsets recalculados el bind pose es la identidad");

    // Mover la traslacion sin recalcular deja la pose deformada: eso es
    // exactamente lo que avisa el panel.
    doc.bones[1].localBind.m[13] = 5.f;
    dash::anim::Skeleton stale;
    for (const bs::Bone& b : doc.bones)
        stale.addBone(b.name, b.parent, b.offsetMatrix, b.localBind);
    stale.bindPoseMatrices(pose);
    ASSERT(std::fabs(pose[1].m[13] - 5.f) < 1e-4f,
           "sin recalcular offsets el hueso queda desplazado en el bind pose");
}

// ─────────────────────────────────────────────────────────────────────────────
int main()
{
    std::printf("=== test_bone_structure ===\n");

    test_hierarchy_from_flat_vector();
    test_validation_reports_every_defect();
    test_topological_sort_keeps_the_hierarchy();
    test_reparent_valid_and_invalid();
    test_rename_and_filter();
    test_trs_decomposition();
    test_skin_index_remap_survives_the_reorder();
    test_file_round_trip_and_sibling_fixups();
    test_offsets_follow_the_bind_pose();

    std::printf("\nResults: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
