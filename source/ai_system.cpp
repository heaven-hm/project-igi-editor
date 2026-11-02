// ai_system.cpp - Runtime AI guard simulation, dual-cone vision, and combat behavior implementation
#include "ai_system.h"
#include "player_separation.h"
#include <algorithm>
#include <cmath>
#include <utility>

namespace igi {

AiSystem::AiSystem() = default;

void AiSystem::Clear() {
    guards_.clear();
    event_queue_.Clear();
    tick_ = 0;
}

void AiSystem::RegisterGuard(const AiGuardEntity& guard) {
    guards_.push_back(guard);
}

AiGuardEntity* AiSystem::FindGuard(uint32_t guard_id) {
    for (auto& guard : guards_) {
        if (guard.id == guard_id) {
            return &guard;
        }
    }
    return nullptr;
}

void AiSystem::SetMovementCollisionQuery(MovementCollisionQuery movement_collision_query) {
    movement_collision_query_ = std::move(movement_collision_query);
}

void AiSystem::SetLineOfSightQuery(LineOfSightQuery line_of_sight_query) {
    line_of_sight_query_ = std::move(line_of_sight_query);
}

void AiSystem::ApplyDamage(uint32_t guard_id, float damage) {
    auto* guard = FindGuard(guard_id);
    if (!guard || guard->state == AiGuardState::Dead) return;
    if (guard->script_invulnerable) return;

    guard->health -= damage;
    if (guard->health <= 0.0f) {
        guard->state = AiGuardState::Dead;
    } else {
        guard->state = AiGuardState::Combat;
        guard->suspicion = 1.0f;
    }
}

AiVisionResult AiSystem::CheckVision(const AiGuardEntity& guard, const glm::vec3& target_pos, bool is_alerted) const {
    if (guard.state == AiGuardState::Dead) return AiVisionResult::None;

    glm::vec3 eye_pos(guard.position.x, guard.position.y, guard.position.z + 1.7f * 4096.0f);
    glm::vec3 delta = target_pos - eye_pos;

    // Facing direction
    float yaw_rad = glm::radians(guard.yaw);
    glm::vec3 facing(std::sin(yaw_rad), std::cos(yaw_rad), 0.0f);
    glm::vec3 right(std::cos(yaw_rad), -std::sin(yaw_rad), 0.0f);
    glm::vec3 up(0.0f, 0.0f, 1.0f);

    float local_forward = glm::dot(delta, facing);
    float local_right   = glm::dot(delta, right);
    float local_up      = glm::dot(delta, up);

    if (local_forward <= 0.0f) return AiVisionResult::None; // Behind guard

    float dist = glm::length(delta);
    float yaw_angle = std::abs(std::atan2(local_right, local_forward));
    float pitch_angle = std::abs(std::atan2(local_up, local_forward));

    const auto& cfg = guard.vision_config;

    // Primary cone test
    float max_primary = is_alerted ? cfg.alert_sight_range : cfg.patrol_sight_range;
    if (dist <= max_primary && yaw_angle < cfg.primary_fov_yaw && pitch_angle < cfg.primary_fov_pitch) {
        return AiVisionResult::Primary;
    }

    // Peripheral cone test
    float max_periph = is_alerted ? cfg.alert_sight_range : cfg.periph_sight_range;
    if (dist <= max_periph && yaw_angle < cfg.periph_fov_yaw && pitch_angle < cfg.periph_fov_pitch) {
        return AiVisionResult::Peripheral;
    }

    return AiVisionResult::None;
}

namespace {
// Unit scale: 1 world meter = 4096 units (PlayerController::WORLD_METER).
constexpr float kMetersToUnits = 4096.0f;
constexpr float kPatrolSpeed = 1.6f * kMetersToUnits; // walk ~1.6 m/s
constexpr float kChaseSpeed  = 4.5f * kMetersToUnits; // run  ~4.5 m/s
constexpr float kWaypointArriveRadius = 180.0f;
constexpr float kGuardCollisionRadius = 0.4f * kMetersToUnits;
// Keep the guard outside the combined player/guard collision envelope while
// still close enough for hitscan combat. The previous tiny threshold let the
// chase overshoot into the player and made the player obstacle resolver push
// both actors through the firefight.
constexpr float kCombatStopDistance = 2.1f * kGuardCollisionRadius;

// OpenIGI patrol constants (AiPatrolRoute / AiMovementStep / AiSoldier).
constexpr int    kNoCommand        = -1;
constexpr int    kNoDeadline       = -1;
constexpr float  kLookAtTolerance  = 0.052359879f;   // 3 degrees
constexpr float  kAimYawClamp      = 0.0257f;        // ~1.47 degrees (look-at)
constexpr double kArrivalThreshold = 1228.8;         // 0.3 m box per horizontal axis
constexpr double kLegEpsilonSq     = 1.0;            // (dx^2 + dy^2) < 1 -> coincident
}

// Wrap an angle into [-pi, pi].
static float WrapRad(float r) {
    while (r > glm::pi<float>())  r -= glm::two_pi<float>();
    while (r < -glm::pi<float>()) r += glm::two_pi<float>();
    return r;
}

namespace {
// World position of a graph node: nodes are local to the AIGraph task's origin.
glm::vec3 NodeWorldPos(const AiGuardEntity& g, const GraphNode& n) {
    return glm::vec3((float)(g.graph_offset.x + n.x),
                     (float)(g.graph_offset.y + n.y),
                     (float)(g.graph_offset.z + n.z));
}

// OpenIGI AiPatrolRoute.Peek (sub_453730): next command, else recorded End, else first.
int PatrolPeek(const AiGuardEntity& g) {
    if (g.command_index >= 0 && g.command_index + 1 < (int)g.patrol_commands.size()) {
        return g.command_index + 1;
    }
    if (g.end_index != kNoCommand) return g.end_index;
    return g.patrol_commands.empty() ? kNoCommand : 0;
}

// OpenIGI AiPatrolRoute.Start (0x4537E0) at startIndex=0.
void PatrolStart(AiGuardEntity& g) {
    g.patrol_started = true;
    if (g.patrol_commands.empty()) { g.patrol_stopped = true; return; }

    int index = 0;
    if (index >= (int)g.patrol_commands.size()) { g.patrol_stopped = true; return; }

    g.command_index = index;
    if (g.patrol_commands[index].kind == AiPatrolCommandKind::End) {
        int next = PatrolPeek(g);
        if (next == kNoCommand) { g.patrol_stopped = true; return; }
        g.command_index = next;
    }
}

// OpenIGI AiPatrolRoute.Advance (sub_453760). Returns false when the patrol stops.
bool PatrolAdvance(AiGuardEntity& g) {
    g.deadline_tick = kNoDeadline;

    int next = PatrolPeek(g);
    if (next == kNoCommand) { g.patrol_stopped = true; return false; }

    AiPatrolCommandKind kind = g.patrol_commands[next].kind;
    if (kind == AiPatrolCommandKind::End) {
        if (next == g.end_index) { g.patrol_stopped = true; return false; }
        int before = next;
        g.command_index = next;
        g.end_index = next;
        next = PatrolPeek(g);
        if (next == kNoCommand || next == before) { g.patrol_stopped = true; return false; }
        kind = g.patrol_commands[next].kind;
        if (kind == AiPatrolCommandKind::End) { g.patrol_stopped = true; return false; }
    }

    if (kind == AiPatrolCommandKind::WalkTo || kind == AiPatrolCommandKind::RunTo) {
        g.prev_move_index = g.last_move_index;
        g.last_move_index = next;
    }
    g.command_index = next;
    return true;
}

// OpenIGI AiPatrolRoute.Begin: applies the command's own cursor effects.
bool PatrolBegin(AiGuardEntity& g, AiPatrolCommandKind kind, int operand, uint64_t tick) {
    switch (kind) {
        case AiPatrolCommandKind::WalkTo:
            g.crouching = false;
            g.walking = true;
            return false;
        case AiPatrolCommandKind::RunTo:
            g.crouching = false;
            g.walking = false;
            return false;
        case AiPatrolCommandKind::Crouch:
            g.crouching = true;
            return true;
        case AiPatrolCommandKind::Delay:
            if (g.deadline_tick == kNoDeadline) {
                g.deadline_tick = operand + (int)tick;
            }
            return tick > (uint64_t)g.deadline_tick;
        default:
            return false;
    }
}

// OpenIGI AiSoldier.TurnTowards: rotate yaw toward `target` by at most yaw_clamp.
// Editor convention: yaw (radians) has forward = (sin yaw, cos yaw).
void TurnTowards(AiGuardEntity& g, const glm::vec3& target, float tolerance) {
    double dx = target.x - g.position.x;
    double dy = target.y - g.position.y;
    if ((dx * dx) + (dy * dy) < 1.0) return;

    float yaw_rad = glm::radians(g.yaw);
    float wanted = (float)std::atan2(dx, dy);
    float error = WrapRad(wanted - yaw_rad);
    if (std::fabs(error) <= tolerance) return;

    float limit = g.yaw_clamp;
    g.yaw = glm::degrees(WrapRad(yaw_rad + std::clamp(error, -limit, limit)));
}

bool IsFacing(const AiGuardEntity& g, const glm::vec3& target, float tolerance) {
    double dx = target.x - g.position.x;
    double dy = target.y - g.position.y;
    if ((dx * dx) + (dy * dy) < 1.0) return true;
    float yaw_rad = glm::radians(g.yaw);
    float wanted = (float)std::atan2(dx, dy);
    return std::fabs(WrapRad(wanted - yaw_rad)) <= tolerance;
}

// OpenIGI AiSoldier.HeightAlongLegTo: interpolate Z along the leg by horizontal progress.
float HeightAlongLegTo(const AiGuardEntity& g, const glm::vec3& waypoint, float x, float y) {
    double legX = waypoint.x - g.leg_origin.x;
    double legY = waypoint.y - g.leg_origin.y;
    double lengthSquared = (legX * legX) + (legY * legY);
    if (lengthSquared <= kLegEpsilonSq) return waypoint.z;

    double travelled = (((x - g.leg_origin.x) * legX) + ((y - g.leg_origin.y) * legY)) / lengthSquared;
    travelled = std::clamp(travelled, 0.0, 1.0);
    return (float)(g.leg_origin.z + ((waypoint.z - g.leg_origin.z) * travelled));
}

// OpenIGI AiSoldier.GoTo: head for a graph node following the route table.
bool GoTo(AiGuardEntity& g, int node) {
    if (!g.graph || g.graph->route_table.empty()) return false;
    if (GRAPH_FindNode(*g.graph, node) == nullptr) return false;
    if (g.current_node < 0) return false;

    if (g.current_node == node) {
        g.route.clear();
        g.destination_node = node;
        g.leg_origin = g.position;
        return true;
    }

    std::vector<int> route = GRAPH_EnumerateRoute(*g.graph, g.current_node, node);
    if (route.empty()) return false;

    g.route = std::move(route);
    g.destination_node = node;
    g.leg_origin = g.position;
    return true;
}

// OpenIGI AiSoldier.Advance: walk one step along the current route leg.
void Advance(AiGuardEntity& g, double delta_seconds) {
    if (g.route.empty()) return;

    const GraphNode* next = GRAPH_FindNode(*g.graph, g.route[0]);
    if (!next) { g.route.erase(g.route.begin()); return; }

    glm::vec3 waypoint = NodeWorldPos(g, *next);

    // Move-to-node entry sets the yaw clamp to pi ("as fast as you like").
    g.yaw_clamp = glm::pi<float>();
    TurnTowards(g, waypoint, 0.0f);

    // Oriented node-box arrival test (AiMovementStep.Step): 0.3 m per horizontal axis.
    float yaw_rad = glm::radians(g.yaw);
    glm::vec3 forward(std::sin(yaw_rad), std::cos(yaw_rad), 0.0f);
    glm::vec3 right(std::cos(yaw_rad), -std::sin(yaw_rad), 0.0f);
    glm::vec3 delta = waypoint - g.position;
    double localX = glm::dot(delta, right);
    double localY = glm::dot(delta, forward);

    if (std::fabs(localX) <= kArrivalThreshold && std::fabs(localY) <= kArrivalThreshold) {
        g.current_node = next->id;
        g.route.erase(g.route.begin());
        g.leg_origin = g.position;
        return;
    }

    // Walk along facing; the distance covered is the gait's speed (no clip root
    // motion in this port). Z follows the leg interpolation, matching OpenIGI's
    // HeightAlongLegTo ground fallback.
    float speed = g.walking ? kPatrolSpeed : kChaseSpeed;
    float dist = speed * static_cast<float>(delta_seconds);
    float x = g.position.x + (forward.x * dist);
    float y = g.position.y + (forward.y * dist);
    g.position = glm::vec3(x, y, HeightAlongLegTo(g, waypoint, x, y));
}

// OpenIGI AiSoldier.StepTowardsNode: one tick of moving toward a graph node.
bool StepTowardsNode(AiGuardEntity& g, int node, double delta_seconds) {
    if (g.destination_node != node || g.route.empty()) {
        if (!GoTo(g, node)) return true;
    }
    if (g.route.empty()) return true;

    Advance(g, delta_seconds);
    return g.route.empty();
}

// OpenIGI AiSoldier.FaceNode: turn to look at a graph node, to within 3 degrees.
bool FaceNode(AiGuardEntity& g, int node) {
    const GraphNode* target = GRAPH_FindNode(*g.graph, node);
    if (!target) return true;

    glm::vec3 world = NodeWorldPos(g, *target);
    g.yaw_clamp = kAimYawClamp;
    TurnTowards(g, world, kLookAtTolerance);
    return IsFacing(g, world, kLookAtTolerance);
}

// Inferred fallback for editor-authored guards that have waypoints but no
// usable PatrolPath/QVM command stream. Parsed vanilla patrol commands take
// precedence; this keeps a playable level alive when only graph data exists.
void AdvanceFallbackPatrol(AiGuardEntity& guard, double delta_seconds) {
    if (guard.waypoints.size() < 2) {
        return;
    }

    const size_t waypoint_count = guard.waypoints.size();
    const size_t target_index = guard.current_waypoint % waypoint_count;
    const glm::vec3 target_position = guard.waypoints[target_index];
    const glm::vec2 horizontal_offset(
        target_position.x - guard.position.x,
        target_position.y - guard.position.y);
    const float horizontal_distance = glm::length(horizontal_offset);
    if (horizontal_distance <= kWaypointArriveRadius) {
        guard.current_waypoint = static_cast<uint32_t>((target_index + 1) % waypoint_count);
        return;
    }

    const glm::vec2 direction = horizontal_offset / horizontal_distance;
    const float travel_distance = kPatrolSpeed * static_cast<float>(delta_seconds);
    const float clamped_travel_distance = std::min(travel_distance, horizontal_distance);
    guard.position.x += direction.x * clamped_travel_distance;
    guard.position.y += direction.y * clamped_travel_distance;
    guard.position.z = HeightAlongLegTo(guard, target_position, guard.position.x, guard.position.y);
    guard.yaw = glm::degrees(std::atan2(direction.x, direction.y));
}

// OpenIGI AiSoldier.RunPatrolCommand: execute the current patrol command.
void RunPatrolCommand(AiGuardEntity& g, uint64_t tick, double delta_seconds) {
    if (g.patrol_stopped) return;
    if (!g.patrol_started) PatrolStart(g);
    if (g.patrol_stopped) return;

    if (g.command_index < 0 || g.command_index >= (int)g.patrol_commands.size()) {
        return;
    }

    const AiPatrolCommand& cmd = g.patrol_commands[g.command_index];
    bool finished = PatrolBegin(g, cmd.kind, cmd.operand, tick);

    switch (cmd.kind) {
        case AiPatrolCommandKind::Animation:
            // The renderer plays the requested clip; without a clip-finished
            // feedback loop the cursor advances immediately rather than stalling.
            g.requested_animation = cmd.operand;
            ++g.animation_request_serial;
            finished = true;
            break;
        case AiPatrolCommandKind::Delay:
            g.requested_animation = -1;
            break;
        case AiPatrolCommandKind::WalkTo:
        case AiPatrolCommandKind::RunTo:
            g.requested_animation = -1;
            finished = StepTowardsNode(g, cmd.operand, delta_seconds);
            break;
        case AiPatrolCommandKind::LookAtNode:
            g.requested_animation = -1;
            finished = FaceNode(g, cmd.operand);
            break;
        case AiPatrolCommandKind::Crouch:
            // Applied by Begin; the one kind that never waits.
            break;
        case AiPatrolCommandKind::Quit:
            g.requested_animation = -1;
            g.patrol_stopped = true;
            return;
        default:
            // No arm in the human walker: the cursor does not advance (SetSpeed wedges).
            return;
    }

    if (finished) {
        g.animation_finished = false;
        PatrolAdvance(g);
    }
}
} // namespace

void AiSystem::Update(
    double delta_seconds,
    const glm::vec3& player_pos,
    bool player_alive) {
    Update(delta_seconds, player_pos, player_pos, player_alive);
}

void AiSystem::Update(
    double delta_seconds,
    const glm::vec3& player_pos,
    const glm::vec3& player_eye_position,
    bool player_alive) {
    // Process acoustic stimuli from event queue
    std::vector<AiStimulusEvent> events;
    event_queue_.Pump(events);

    for (auto& guard : guards_) {
        if (!guard.runtime_enabled || guard.state == AiGuardState::Dead) {
            continue;
        }

        const glm::vec3 previous_position = guard.position;

        // 1. Process hearing (always active: vanilla guards wake on loud
        // events such as hard ground impacts).
        for (const auto& evt : events) {
            // OpenIGI's per-soldier event queues reject records naming the
            // receiving AI as their source/subject. RuntimeWorld uses one
            // shared queue, so preserve that owner filter explicitly; a
            // firing guard must not hear its own gunshot on the next tick.
            if (evt.originator_id != 0 && evt.originator_id == guard.id) {
                continue;
            }

            float dist = glm::distance(guard.position, evt.position);
            if (evt.hearing_radius_units > 0.0f) {
                if (dist > evt.hearing_radius_units) {
                    continue;
                }
                guard.state = AiGuardState::Suspicious;
                guard.suspicion = 1.0f;
                continue;
            }

            float heard_loudness = evt.loudness / (1.0f + 0.05f * dist);
            if (heard_loudness > 0.35f) {
                guard.state = AiGuardState::Suspicious;
                guard.suspicion = 1.0f;
            }
        }

        // 2. Process vision (alarm-gated; see hearing above).
        if (player_alive && alarm_active_) {
            AiVisionResult vis = CheckVision(
                guard,
                player_eye_position,
                guard.state == AiGuardState::Combat);
            if (vis != AiVisionResult::None && line_of_sight_query_) {
                const glm::vec3 guard_eye_position = guard.position + glm::vec3(
                    0.0f,
                    0.0f,
                    1.7f * 4096.0f);
                if (!line_of_sight_query_(guard_eye_position, player_eye_position)) {
                    vis = AiVisionResult::None;
                }
            }
            if (vis == AiVisionResult::Primary) {
                guard.state = AiGuardState::Combat;
                guard.suspicion = 1.0f;
            } else if (vis == AiVisionResult::Peripheral) {
                guard.suspicion = std::min(1.0f, guard.suspicion + 0.2f);
                if (guard.suspicion >= 0.8f) {
                    guard.state = AiGuardState::Suspicious;
                }
            }
        }

        // 3. Patrol / chase movement
        float speed = kPatrolSpeed;
        if (guard.state == AiGuardState::Combat) speed = kChaseSpeed;

        // Suspicion decays over time; returning to patrol when it dissipates.
        if (guard.state == AiGuardState::Suspicious) {
            guard.suspicion = std::max(0.0f, guard.suspicion - static_cast<float>(0.15 * delta_seconds));
            if (guard.suspicion == 0.0f) {
                guard.state = AiGuardState::Patrol;
                guard.current_waypoint = 0;
            }
        }

        if (guard.state == AiGuardState::Combat && player_alive) {
            // Vanilla guard detection: take a SHOOTING POSITION — hold ground,
            // track the player with yaw. Guards never chase-run at the player;
            // running is the civilian panic reaction only.
            glm::vec3 to_player = player_pos - guard.position;
            if (glm::length(glm::vec2(to_player.x, to_player.y)) > 1.0f) {
                guard.yaw = glm::degrees(std::atan2(to_player.x, to_player.y));
            }
        } else if (guard.state == AiGuardState::Suspicious && !guard.waypoints.empty()) {
            // Slowly advance toward the nearest waypoint while investigating
            glm::vec3 target_wp = guard.waypoints[guard.current_waypoint];
            glm::vec3 to_wp = target_wp - guard.position;
            float wp_dist = glm::length(glm::vec2(to_wp.x, to_wp.y));
            if (wp_dist < kWaypointArriveRadius) {
                guard.current_waypoint = (guard.current_waypoint + 1) % guard.waypoints.size();
            } else {
                glm::vec2 dir = glm::normalize(glm::vec2(to_wp.x, to_wp.y));
                guard.position.x += dir.x * kPatrolSpeed * static_cast<float>(delta_seconds);
                guard.position.y += dir.y * kPatrolSpeed * static_cast<float>(delta_seconds);
                guard.yaw = glm::degrees(std::atan2(dir.x, dir.y));
            }
        } else if (guard.state == AiGuardState::Patrol) {
            // OpenIGI patrol port: walk the authored PatrolPath commands along the
            // graph route table. Guards with no patrol data stand guard in place.
            guard.tick = tick_;
            if (!guard.patrol_commands.empty()) {
                RunPatrolCommand(guard, tick_, delta_seconds);
            } else if (!guard.graph) {
                AdvanceFallbackPatrol(guard, delta_seconds);
            }
        }

        // Human separation (vanilla A11): a move that ends nearer to another
        // human than it started is rejected outright. Guards block against the
        // player and every other live guard without being displaced.
        if (guard.position != previous_position) {
            std::vector<glm::vec3> blockers;
            blockers.reserve(guards_.size());
            for (const auto& other : guards_) {
                if (other.id != guard.id && other.state != AiGuardState::Dead &&
                    other.runtime_enabled) {
                    blockers.push_back(other.position);
                }
            }
            if (player_alive) {
                blockers.push_back(player_pos);
            }
            guard.position = HumanSeparation::ResolveAll(
                previous_position,
                guard.position,
                blockers.begin(),
                blockers.end());
        }

        // Vanilla AI has no per-move world collision probe: graph-following
        // movement is authored-safe via the route table, and body blocking is
        // handled by the separation pass above. The previous AABB probe here
        // rejected nav nodes inside buildings and froze patrols permanently.
        guard.blocked_move_ticks = 0;

        // Locomotion clip request from the soldier state table (retail
        // 0x53ED10): normal patrol locomotion is the looping WALK, animation
        // 34 (state 1). Animation 48 is state 17, the alarm/panic RUN — it
        // must never play as a default idle-patrol gait, so combat chase
        // keeps the walk clip until the alarm-driven panic state lands.
        if (guard.requested_animation < 0 && guard.state != AiGuardState::Combat) {
            const bool moved = guard.position != previous_position;
            const int desired = moved ? 34 : guard.stand_animation;
            if (desired != guard.locomotion_anim) {
                guard.locomotion_anim = desired;
                guard.requested_animation = desired;
                ++guard.animation_request_serial;
            }
        }
    }

    // 4. Advance the 30 Hz simulation tick.
    tick_ += static_cast<uint64_t>(std::max(1, (int)std::lround(delta_seconds * 30.0)));
}

} // namespace igi
