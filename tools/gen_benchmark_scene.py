#!/usr/bin/env python3
"""Genera una escena de benchmark para medir el render 3D (backlog A2).

Coloca N entidades en una grilla centrada en el jugador, con separacion chica
para que todas caigan dentro del frustum. Sirve de baseline reproducible para
comparar cambios en el path de dibujo (por ejemplo, instancing / A1).

    python3 tools/gen_benchmark_scene.py --count 400 --spacing 0.6 \
        --out scenes/benchmark_dense.json
"""

import argparse
import json
import math


SCENE_VERSION = 6
CENTER = 32.0


def transform(x, y, scale):
    return {
        "type": "Transform",
        "x": x,
        "y": y,
        "z": 0.0,
        "yawDeg": 0.0,
        "pitchDeg": 0.0,
        "rollDeg": 0.0,
        "scale": scale,
    }


def render(mesh):
    return {
        "type": "Render",
        "renderMode": 0,
        "mesh": mesh,
        "material": "default",
        "sprite": "default",
        "layer": 0,
        "visible": True,
    }


def build(count, spacing, scale, mesh):
    entities = [
        {
            "id": 1,
            "type": "Player",
            "name": "Hero",
            "class": "Warrior",
            "x": CENTER,
            "y": CENTER,
            "components": [transform(CENTER, CENTER, 1.0), render("cube")],
        }
    ]

    # Grilla cuadrada centrada en el jugador: maximiza las entidades simultaneamente
    # visibles, que es el escenario que A1 (instancing) pretende optimizar.
    side = int(math.ceil(math.sqrt(count)))
    half = (side - 1) / 2.0
    placed = 0
    for row in range(side):
        for col in range(side):
            if placed >= count:
                break
            x = CENTER + (col - half) * spacing
            y = CENTER + (row - half) * spacing
            placed += 1
            entities.append(
                {
                    "id": 1 + placed,
                    "type": "Enemy",
                    "name": f"Bench{placed:04d}",
                    "x": x,
                    "y": y,
                    "components": [transform(x, y, scale), render(mesh)],
                }
            )

    return {
        "sceneVersion": SCENE_VERSION,
        "name": "benchmark_dense",
        "worldSeed": 12345,
        "nextEntityId": len(entities) + 1,
        "tileOverrides": [],
        "vertexHeightOverrides": [],
        "cliffOverrides": [],
        "textureOverrides": [],
        "waterBodies": [],
        "entities": entities,
    }


def main():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--count", type=int, default=400,
                   help="entidades ademas del jugador (default: 400)")
    p.add_argument("--spacing", type=float, default=0.6,
                   help="separacion en unidades de mundo (default: 0.6)")
    p.add_argument("--scale", type=float, default=1.0,
                   help="escala por entidad (default: 1.0)")
    p.add_argument("--mesh", default="cube",
                   help="mesh id de RenderComponent (default: cube)")
    p.add_argument("--out", default="scenes/benchmark_dense.json")
    args = p.parse_args()

    scene = build(args.count, args.spacing, args.scale, args.mesh)
    with open(args.out, "w") as f:
        json.dump(scene, f, indent=2)
        f.write("\n")

    print(f"{args.out}: {len(scene['entities'])} entidades "
          f"(spacing={args.spacing}, scale={args.scale}, mesh={args.mesh})")


if __name__ == "__main__":
    main()
