# 12 — Math Essentials for 2D Top-Down Games

## What You Need (and Don't Need)

**DO use**: 2D vectors, 2D matrices (for transforms), rotation angles, AABBs, circles, random numbers.
**DON'T need**: Quaternions, 3D matrices, skeletal math, SIMD (Raylib handles this internally).

Raylib provides `Vector2` and basic operations. Extend as needed.

## 2D Vectors

Raylib's `Vector2` is `{ float x, y }`. Key operations:

```c
// Raylib provides these, but here's what they do:
Vector2 Vector2Add(Vector2 a, Vector2 b);       // a + b
Vector2 Vector2Subtract(Vector2 a, Vector2 b);  // a - b
Vector2 Vector2Scale(Vector2 v, float s);        // v * s
float   Vector2Length(Vector2 v);                 // |v|
float   Vector2LengthSqr(Vector2 v);            // |v|² (avoids sqrt)
Vector2 Vector2Normalize(Vector2 v);             // v / |v|
float   Vector2DotProduct(Vector2 a, Vector2 b); // a·b
float   Vector2Distance(Vector2 a, Vector2 b);   // |a - b|
float   Vector2Angle(Vector2 a, Vector2 b);      // angle between
Vector2 Vector2Lerp(Vector2 a, Vector2 b, float t); // linear interpolation
```

### Additional 2D utilities you may need:

```c
// Perpendicular vector (90° CCW rotation)
Vector2 vec2_perp(Vector2 v) {
    return (Vector2){ -v.y, v.x };
}

// 2D cross product (scalar result: useful for winding/side tests)
float vec2_cross(Vector2 a, Vector2 b) {
    return a.x * b.y - a.y * b.x;
}

// Rotate a vector by an angle (radians)
Vector2 vec2_rotate(Vector2 v, float angle_rad) {
    float c = cosf(angle_rad);
    float s = sinf(angle_rad);
    return (Vector2){ v.x * c - v.y * s, v.x * s + v.y * c };
}

// Direction from angle (0 = right, PI/2 = down in screen coords)
Vector2 vec2_from_angle(float angle_rad) {
    return (Vector2){ cosf(angle_rad), sinf(angle_rad) };
}

// Angle from direction vector
float vec2_to_angle(Vector2 dir) {
    return atan2f(dir.y, dir.x);
}

// Move toward target at a given speed, stopping at the target
Vector2 vec2_move_toward(Vector2 current, Vector2 target, float max_dist) {
    Vector2 diff = Vector2Subtract(target, current);
    float dist = Vector2Length(diff);
    if (dist <= max_dist || dist < 0.0001f) return target;
    return Vector2Add(current, Vector2Scale(diff, max_dist / dist));
}

// Reflect a vector off a surface with the given normal
Vector2 vec2_reflect(Vector2 v, Vector2 normal) {
    float d = 2.0f * Vector2DotProduct(v, normal);
    return Vector2Subtract(v, Vector2Scale(normal, d));
}
```

## Collision Primitives

### AABB (Axis-Aligned Bounding Box)

Raylib uses `Rectangle` for AABBs: `{ float x, y, width, height }`.

```c
// Raylib provides:
bool CheckCollisionRecs(Rectangle a, Rectangle b);
Rectangle GetCollisionRec(Rectangle a, Rectangle b); // overlap region

// Entity world-space collider
Rectangle entity_world_collider(const Entity* e) {
    return (Rectangle){
        e->position.x + e->collider.x,
        e->position.y + e->collider.y,
        e->collider.width,
        e->collider.height
    };
}
```

### Circle Colliders

```c
// Raylib provides:
bool CheckCollisionCircles(Vector2 c1, float r1, Vector2 c2, float r2);
bool CheckCollisionCircleRec(Vector2 center, float radius, Rectangle rec);
```

### Point-in-Shape Tests

```c
bool CheckCollisionPointRec(Vector2 point, Rectangle rec);     // Raylib
bool CheckCollisionPointCircle(Vector2 point, Vector2 center, float radius); // Raylib
```

### Line/Ray Intersection

Useful for line-of-sight checks:

```c
// Check if line segment (p1→p2) intersects rectangle
bool line_rect_intersect(Vector2 p1, Vector2 p2, Rectangle rect, Vector2* hit_point) {
    // Test against all 4 edges of the rectangle
    Vector2 corners[4] = {
        { rect.x, rect.y },
        { rect.x + rect.width, rect.y },
        { rect.x + rect.width, rect.y + rect.height },
        { rect.x, rect.y + rect.height }
    };
    
    float closest_t = 1e30f;
    bool any_hit = false;
    
    for (int i = 0; i < 4; i++) {
        Vector2 a = corners[i];
        Vector2 b = corners[(i + 1) % 4];
        
        // Line-segment intersection math
        float denom = (p2.x - p1.x) * (b.y - a.y) - (p2.y - p1.y) * (b.x - a.x);
        if (fabsf(denom) < 1e-8f) continue; // parallel
        
        float t = ((a.x - p1.x) * (b.y - a.y) - (a.y - p1.y) * (b.x - a.x)) / denom;
        float u = ((a.x - p1.x) * (p2.y - p1.y) - (a.y - p1.y) * (p2.x - p1.x)) / denom;
        
        if (t >= 0.0f && t <= 1.0f && u >= 0.0f && u <= 1.0f) {
            if (t < closest_t) {
                closest_t = t;
                any_hit = true;
            }
        }
    }
    
    if (any_hit && hit_point) {
        hit_point->x = p1.x + closest_t * (p2.x - p1.x);
        hit_point->y = p1.y + closest_t * (p2.y - p1.y);
    }
    return any_hit;
}
```

## Random Number Generation

Raylib provides `GetRandomValue(min, max)` (integer). For game-quality randomness with seedable state:

```c
// Simple xorshift32 — fast, good distribution, seedable
typedef struct RNG {
    uint32_t state;
} RNG;

void rng_seed(RNG* rng, uint32_t seed) {
    rng->state = seed ? seed : 1; // must be non-zero
}

uint32_t rng_next(RNG* rng) {
    uint32_t x = rng->state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng->state = x;
    return x;
}

// Random float in [0, 1)
float rng_float(RNG* rng) {
    return (rng_next(rng) >> 8) / 16777216.0f; // 24-bit mantissa
}

// Random float in [min, max)
float rng_float_range(RNG* rng, float min, float max) {
    return min + rng_float(rng) * (max - min);
}

// Random int in [min, max] inclusive
int rng_int_range(RNG* rng, int min, int max) {
    return min + (int)(rng_next(rng) % (uint32_t)(max - min + 1));
}

// Random point in circle
Vector2 rng_point_in_circle(RNG* rng, Vector2 center, float radius) {
    float angle = rng_float(rng) * 2.0f * PI;
    float r = radius * sqrtf(rng_float(rng)); // sqrt for uniform distribution
    return (Vector2){ center.x + r * cosf(angle), center.y + r * sinf(angle) };
}
```

## Interpolation / Easing

Essential for smooth movement, UI animations, camera follow:

```c
// Linear interpolation
float lerpf(float a, float b, float t) {
    return a + (b - a) * t;
}

// Smoothstep (ease in-out)
float smoothstep(float t) {
    return t * t * (3.0f - 2.0f * t);
}

// Exponential decay (great for camera follow)
float exp_decay(float current, float target, float speed, float dt) {
    return target + (current - target) * expf(-speed * dt);
}

// Same for Vector2
Vector2 vec2_exp_decay(Vector2 current, Vector2 target, float speed, float dt) {
    float f = expf(-speed * dt);
    return (Vector2){
        target.x + (current.x - target.x) * f,
        target.y + (current.y - target.y) * f
    };
}
```

## Practical Tips

- **Use `Vector2LengthSqr` instead of `Vector2Length`** when comparing distances. Avoids the expensive `sqrt`.
- **`atan2f(y, x)` returns radians in [-π, π]**. In screen coordinates (Y-down), 0 = right, π/2 = down.
- **Radians internally, degrees only for display**: Raylib's draw functions expect degrees (via `RAD2DEG`), but all math should use radians.
- **Seed your RNG deterministically** for reproducible test scenarios. Use a known seed during debugging.
