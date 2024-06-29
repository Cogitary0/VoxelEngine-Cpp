#include "Entities.hpp"

#include "../assets/Assets.hpp"
#include "../world/Level.hpp"
#include "../physics/Hitbox.hpp"
#include "../physics/PhysicsSolver.hpp"
#include "../graphics/render/ModelBatch.hpp"
#include "../graphics/core/LineBatch.hpp"
#include "../graphics/core/Model.hpp"
#include "../maths/FrustumCulling.hpp"
#include "../objects/EntityDef.hpp"
#include "../logic/scripting/scripting.hpp"

#include <glm/ext/matrix_transform.hpp>

void Transform::refresh() {
    combined = glm::mat4(1.0f);
    combined = glm::translate(combined, pos);
    combined = glm::scale(combined, size);
    combined = combined * glm::mat4(rot);
}

void Entity::destroy() {
    if (isValid()){
        entities.despawn(id);
    }
}

Entities::Entities(Level* level) : level(level) {
}

entityid_t Entities::spawn(EntityDef& def, glm::vec3 pos) {
    auto entity = registry.create();
    glm::vec3 size(1);
    auto id = nextID++;
    registry.emplace<EntityId>(entity, static_cast<entityid_t>(id), def);
    registry.emplace<Transform>(entity, pos, size/4.0f, glm::mat3(1.0f));
    registry.emplace<Rigidbody>(entity, true, Hitbox {pos, def.hitbox});
    auto& scripting = registry.emplace<Scripting>(entity, entity_funcs_set {}, nullptr);
    entities[id] = entity;
    scripting.env = scripting::on_entity_spawn(def, id, scripting.funcsset);
    return id;
}

void Entities::despawn(entityid_t id) {
    if (auto entity = get(id)) {
        scripting::on_entity_despawn(entity->getDef(), *entity);
        registry.destroy(get(id)->getHandler());
    }
}

void Entities::clean() {
    for (auto it = entities.begin(); it != entities.end();) {
        if (registry.valid(it->second)) {
            ++it;
        } else {
            it = entities.erase(it);
        }
    }
}

void Entities::updatePhysics(float delta){
    auto view = registry.view<EntityId, Transform, Rigidbody>();
    auto physics = level->physics.get();
    for (auto [entity, eid, transform, rigidbody] : view.each()) {
        if (!rigidbody.enabled) {
            continue;
        }
        auto& hitbox = rigidbody.hitbox;
        auto prevVel = hitbox.velocity;
        bool grounded = hitbox.grounded;
        physics->step(
            level->chunks.get(),
            &hitbox,
            delta,
            10,
            false,
            1.0f,
            true
        );
        hitbox.linearDamping = hitbox.grounded * 24;
        transform.pos = hitbox.position;
        //transform.rot = glm::rotate(glm::mat4(transform.rot), delta, glm::vec3(0, 1, 0));
        if (hitbox.grounded && !grounded) {
            scripting::on_entity_grounded(*get(eid.uid), glm::length(prevVel-hitbox.velocity));
        }
        if (!hitbox.grounded && grounded) {
            scripting::on_entity_fall(*get(eid.uid));
        }
    }
}

void Entities::update() {
    scripting::on_entities_update();
}

void Entities::renderDebug(LineBatch& batch) {
    batch.lineWidth(1.0f);
    auto view = registry.view<Transform, Rigidbody>();
    for (auto [entity, transform, rigidbody] : view.each()) {
        const auto& hitbox = rigidbody.hitbox;
        batch.box(hitbox.position, hitbox.halfsize * 2.0f, glm::vec4(1.0f));
    }
}

void Entities::render(Assets* assets, ModelBatch& batch, Frustum& frustum) {
    auto view = registry.view<Transform>();
    auto model = assets->get<model::Model>("cube");
    for (auto [entity, transform] : view.each()) {
        const auto& pos = transform.pos;
        const auto& size = transform.size;
        if (frustum.isBoxVisible(pos-size, pos+size)) {
            transform.refresh();
            batch.pushMatrix(transform.combined);
            batch.draw(model);
            batch.popMatrix();
        }
    }
}
