/* ============================================================
 * @deps-implements: particle.h
 * @deps-requires: particle.h (ParticleSystem, Particle),
 *                 raylib.h (GetRandomValue, DEG2RAD, Fade),
 *                 math.h (cosf, sinf), string.h (memset)
 * @deps-last-changed: 2026-03-17 — Particle system implementation
 * ============================================================ */

#include "particle.h"

#include <math.h>
#include <string.h>

void particle_init(ParticleSystem *ps)
{
    memset(ps, 0, sizeof(*ps));
}

void particle_spawn_burst_color(ParticleSystem *ps, Vector2 origin,
                                int count, Color base_color)
{
    if (count > MAX_PARTICLES) count = MAX_PARTICLES;

    int spawned = 0;
    for (int i = 0; i < MAX_PARTICLES && spawned < count; i++) {
        if (ps->particles[i].active) continue;

        Particle *p = &ps->particles[i];
        p->active = true;
        p->position = origin;

        float angle_deg = (float)GetRandomValue(0, 360);
        float speed = (float)GetRandomValue(100, 280);
        float angle_rad = angle_deg * DEG2RAD;
        p->velocity = (Vector2){
            cosf(angle_rad) * speed,
            sinf(angle_rad) * speed
        };

        p->lifetime = 0.6f + (float)GetRandomValue(0, 60) / 100.0f;
        if (p->lifetime < 0.01f) p->lifetime = 0.01f;
        p->age = 0.0f;

        p->start_radius = 3.0f + (float)GetRandomValue(0, 40) / 10.0f;
        p->radius = p->start_radius;

        /* Vary the base color slightly for a natural look */
        int rv = GetRandomValue(-30, 30);
        int gv = GetRandomValue(-30, 30);
        int bv = GetRandomValue(-30, 30);
        int r = base_color.r + rv; if (r < 0) r = 0; if (r > 255) r = 255;
        int g = base_color.g + gv; if (g < 0) g = 0; if (g > 255) g = 255;
        int b = base_color.b + bv; if (b < 0) b = 0; if (b > 255) b = 255;
        p->color = (Color){(unsigned char)r, (unsigned char)g,
                           (unsigned char)b, 255};

        spawned++;
    }
    ps->active_count += spawned;
}

void particle_spawn_burst(ParticleSystem *ps, Vector2 origin, int count)
{
    /* Red/pink burst for hearts */
    particle_spawn_burst_color(ps, origin, count,
                               (Color){230, 50, 40, 255});
}

void particle_update(ParticleSystem *ps, float dt)
{
    if (ps->active_count == 0) return;

    int alive = 0;
    for (int i = 0; i < MAX_PARTICLES; i++) {
        Particle *p = &ps->particles[i];
        if (!p->active) continue;

        p->age += dt;
        if (p->age >= p->lifetime) {
            p->active = false;
            continue;
        }

        p->position.x += p->velocity.x * dt;
        p->position.y += p->velocity.y * dt;

        float drag = powf(0.97f, dt * 60.0f);
        p->velocity.x *= drag;
        p->velocity.y *= drag;

        float t = p->age / p->lifetime;
        p->radius = p->start_radius * (1.0f - t);

        alive++;
    }
    ps->active_count = alive;
}

void particle_draw(const ParticleSystem *ps)
{
    for (int i = 0; i < MAX_PARTICLES; i++) {
        const Particle *p = &ps->particles[i];
        if (!p->active) continue;

        float t = p->age / p->lifetime;
        float alpha = 1.0f - t;
        Color c = Fade(p->color, alpha);
        DrawCircleV(p->position, p->radius, c);
    }
}

bool particle_any_active(const ParticleSystem *ps)
{
    return ps->active_count > 0;
}
