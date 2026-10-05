# Octarine Engine Documentation

Welcome to the API documentation for the **Octarine Engine**!

Octarine Engine is a lightweight, high-performance 2D game engine built with C++20. It features a custom archetype-based Entity-Component System (ECS), integrated Lua 5.4 scripting via sol2, SDL3 multimedia backends, Dear ImGui editor tooling, and a signature yellowish-purple aesthetic.

## Key Subsystems

* **ECS & Memory Architecture:** 64-bit generational handles, 16KB 64-byte aligned SoA chunks, active/inactive partition shuffling, archetype transition graph, inverted index query matching, and lock-free command buffers.
* **Lua 5.4 Scripting:** Zero-copy in-chunk proxy access via `sol2`, domain modules, declarative hierarchy trees, and state-preserving script hot reloading.
* **Rendering Architecture:** Lock-free multi-producer render queue, 64-bit packed sort key layout, frustum culling, and sprite render cache powered by SDL3.
* **Simulation Pipeline:** Kahn's topological sort system DAG, two-tier Transform system, and median-cut/SAT Collision system.
* **Asset Pipeline:** Dual-path catalog loading, headless reference validation, texture atlas baker, BS.1770 audio normalization, and `AssetPak` archives.

Explore the classes, namespaces, and files using the sidebar to dive deeper into the engine's internals.
