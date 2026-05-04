#pragma once
#include <vector>
#include <cmath>

// A simple 2D Vector struct for native C++ (removing Qt dependencies)
struct Vec2 {
    float x, y;
    Vec2() : x(0), y(0) {}
    Vec2(float x, float y) : x(x), y(y) {}
    Vec2 operator+(const Vec2& v) const { return Vec2(x + v.x, y + v.y); }
    Vec2 operator-(const Vec2& v) const { return Vec2(x - v.x, y - v.y); }
    Vec2 operator*(float n) const { return Vec2(x * n, y * n); }
    Vec2& operator+=(const Vec2& v) { x += v.x; y += v.y; return *this; }
};

class RigidBody {
public:
    // Linear Mechanics
    Vec2 position;
    Vec2 velocity;
    Vec2 forceAccumulator;
    float mass;
    float inverseMass;

    // Angular Mechanics
    float angle;           // In radians
    float angularVelocity;
    float torqueAccumulator;
    float inertia;
    float inverseInertia;

    // Material Properties
    float restitution;     // Bounciness (0.0 to 1.0)
    float friction;

    RigidBody(Vec2 pos, float m, float radius) : position(pos), mass(m), restitution(0.8f), friction(0.5f) {
        velocity = Vec2(0, 0);
        forceAccumulator = Vec2(0, 0);
        angle = 0.0f;
        angularVelocity = 0.0f;
        torqueAccumulator = 0.0f;

        if (mass == 0.0f) {
            inverseMass = 0.0f;
            inertia = 0.0f;
            inverseInertia = 0.0f;
        } else {
            inverseMass = 1.0f / mass;
            // Moment of inertia for a solid circle: I = 1/2 * m * r^2
            inertia = 0.5f * mass * (radius * radius);
            inverseInertia = 1.0f / inertia;
        }
    }

    void addForce(const Vec2& force) {
        forceAccumulator += force;
    }

    void addTorque(float torque) {
        torqueAccumulator += torque;
    }
};

class LogiaWorld {
public:
    Vec2 gravity;
    std::vector<RigidBody> bodies;

    LogiaWorld() : gravity(0.0f, 981.0f) {} // 9.81 * 100 pixels per meter

    void addBody(const RigidBody& body) {
        bodies.push_back(body);
    }

    void step(float deltaTime) {
        for (auto& body : bodies) {
            if (body.inverseMass == 0.0f) continue; // Static object

            // 1. Apply Gravity
            body.addForce(gravity * body.mass);

            // 2. Linear Integration (Symplectic Euler)
            Vec2 acceleration = body.forceAccumulator * body.inverseMass;
            body.velocity += acceleration * deltaTime;
            body.position += body.velocity * deltaTime;

            // 3. Angular Integration
            float angularAcceleration = body.torqueAccumulator * body.inverseInertia;
            body.angularVelocity += angularAcceleration * deltaTime;
            body.angle += body.angularVelocity * deltaTime;

            // 4. Clear accumulators
            body.forceAccumulator = Vec2(0, 0);
            body.torqueAccumulator = 0.0f;

            // 5. Basic Hardcoded Floor Collision (Y = 500)
            if (body.position.y > 500.0f) {
                body.position.y = 500.0f;
                body.velocity.y *= -body.restitution;
                
                // Simulate simple friction on the floor
                body.velocity.x *= (1.0f - body.friction * deltaTime);
            }
        }
    }
};


