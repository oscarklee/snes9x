#ifndef SPRING_ANIMATION_H
#define SPRING_ANIMATION_H

#include <cmath>

class SpringAnimation {
public:
    float position;
    float target;
    float speed;
    
    SpringAnimation(float stiffness = 0, float damping = 0, float mass = 0)
        : position(0.0f), target(0.0f), speed(15.0f) {}
    
    void setTarget(float newTarget) {
        target = newTarget;
    }
    
    void setPosition(float newPosition) {
        position = newPosition;
    }
    
    void update(float dt) {
        float diff = target - position;
        if (std::abs(diff) < 0.0001f) {
            position = target;
        } else {
            // Smooth movement without bounce
            position += diff * speed * dt;
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
