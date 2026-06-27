// Build script for the pithddu firmware (Rust / esp-idf-sys).
//
// Phase 0: just emit the esp-idf-sys link flags. Phase 1 adds codegen of
// src/gen/field_registry.rs from main/field_registry.json (the single source of
// truth shared by path with the pithddu-dashboard app).

fn main() {
    embuild::espidf::sysenv::output();
}
