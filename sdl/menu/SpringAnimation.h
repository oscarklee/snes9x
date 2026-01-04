#ifndef SPRING_ANIMATION_H
#define SPRING_ANIMATION_H

#include <cmath>

class SpringAnimation {
public:
    float position;
    float target;
    float velocity;
    float stiffness;
    float damping;
    
    SpringAnimation()
        : position(0.0f), target(0.0f), velocity(0.0f), 
          stiffness(120.0f), damping(14.0f) {}
    
    void setTarget(float newTarget) {
        target = newTarget;
    }
    
    void setPosition(float newPosition) {
        position = newPosition;
        velocity = 0.0f;
    }
    
    void update(float dt) {
        // Simple spring physics: F = -k(x-target) - c*v
        float force = -stiffness * (position - target) - damping * velocity;
        velocity += force * dt;
        position += velocity * dt;

        if (std::abs(target - position) < 0.001f && std::abs(velocity) < 0.01f) {
            position = target;
            velocity = 0.0f;
        }
    }
    
    bool isAtRest() const {
        return std::abs(target - position) < 0.0001f;
    }
    
    float getPosition() const {
        return position;
    }
    
    float getTarget() const {
        return target;
    }
};

#endif
