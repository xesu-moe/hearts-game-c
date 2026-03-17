/* ============================================================
 * @deps-exports: struct Particle, struct ParticleSystem,
 *                particle_init(), particle_spawn_burst(),
 *                particle_update(), particle_draw(),
 *                particle_any_active(), MAX_PARTICLES
 * @deps-requires: raylib.h (Vector2, Color), stdbool.h
 * @deps-used-by: render.h, render.c, main.c
 * @deps-last-changed: 2026-03-17 — Created particle system
 * ============================================================ */

#ifndef PARTICLE_H
#define PARTICLE_H

#include <stdbool.h>

#include "raylib.h"

#define MAX_PARTICLES 64

typedef struct Particle {
    Vector2 position;
    Vector2 velocity;
    float   lifetime;
    float   age;
    float   radius;
    float   start_radius;
    Color   color;
    bool    active;
} Particle;

typedef struct ParticleSystem {
    Particle particles[MAX_PARTICLES];
    int      active_count;
} ParticleSystem;

void particle_init(ParticleSystem *ps);
void particle_spawn_burst(ParticleSystem *ps, Vector2 origin, int count);
void particle_update(ParticleSystem *ps, float dt);
void particle_draw(const ParticleSystem *ps);
bool particle_any_active(const ParticleSystem *ps);

#endif /* PARTICLE_H */
