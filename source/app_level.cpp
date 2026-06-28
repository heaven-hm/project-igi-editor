/******************************************************************************
 * @file    app_level.cpp
 * @brief   App: level load/save, game-level sync, ATTA flush, texture-map export
 *          Split from app.cpp; shares app_internal.h.
 *****************************************************************************/
#include "app_internal.h"
#include "utils_igi1conv.h"
#include <mmsystem.h>
#include <future>
#include <set>

void App::PlayLevelMusic(int level_no) {
    StopLevelMusic();

    // Per-level override from qedconfig.qsc (QEDLevelMusic(level, "filename.wav")),
    // falling back to the game's default "game_music.wav" if unset.
    std::string musicFile = "game_music.wav";
    auto& musicMap = Config::Get().levelMusicFiles;
    auto cfgIt = musicMap.find(level_no);
    if (cfgIt != musicMap.end() && !cfgIt->second.empty()) musicFile = cfgIt->second;

    std::string soundsDir = Utils::GetIGIRootPath() + "\\missions\\location0\\level" +
        std::to_string(level_no) + "\\sounds";
    std::string srcWav = soundsDir + "\\" + musicFile;
    if (!std::filesystem::exists(srcWav)) {
        Logger::Get().Log(LogLevel::ERR, "[Music] Configured music file not found: " + srcWav +
            " (searched " + soundsDir + ")");
        return;
    }

    std::string outWav = Utils::GetExeDirectory() + "\\cache\\music\\level" + std::to_string(level_no) +
        "_" + std::filesystem::path(musicFile).stem().string() + ".wav";
    if (!std::filesystem::exists(outWav)) {
        Logger::Get().Log(LogLevel::INFO, "[Music] Converting " + srcWav + " -> " + outWav);
        std::string err;
        if (!igi1conv::WavConvert(srcWav, outWav, err)) {
            Logger::Get().Log(LogLevel::WARNING, "[Music] Convert failed: " + err);
            return;
        }
    }

    // Defensively close any stale alias left over from a previous run that
    // didn't shut down cleanly (e.g. killed by --verify-level's timeout).
    mciSendStringA("close bgmusic", nullptr, 0, nullptr);

    std::string openCmd = "open \"" + outWav + "\" type waveaudio alias bgmusic";
    MCIERROR rc = mciSendStringA(openCmd.c_str(), nullptr, 0, nullptr);
    if (rc != 0) {
        char buf[256] = {};
        mciGetErrorStringA(rc, buf, sizeof(buf));
        Logger::Get().Log(LogLevel::WARNING, "[Music] mci open failed (" + std::to_string(rc) + "): " + buf);
        return;
    }

    // MCI's "repeat" flag is unreliable for plain waveaudio devices, so loop
    // manually: CheckMusicLoop() (called every frame) restarts playback from
    // 0 once the device reports "stopped".
    rc = mciSendStringA("play bgmusic from 0", nullptr, 0, nullptr);
    if (rc != 0) {
        char buf[256] = {};
        mciGetErrorStringA(rc, buf, sizeof(buf));
        Logger::Get().Log(LogLevel::WARNING, "[Music] mci play failed (" + std::to_string(rc) + "): " + buf);
        mciSendStringA("close bgmusic", nullptr, 0, nullptr);
        return;
    }

    music_playing_ = true;
    Logger::Get().Log(LogLevel::INFO, "[Music] Playing level " + std::to_string(level_no) + " music (looping): " + outWav);
}

void App::CheckMusicLoop() {
    if (!music_playing_) return;
    char status[64] = {};
    MCIERROR rc = mciSendStringA("status bgmusic mode", status, sizeof(status), nullptr);
    if (rc != 0) return; // device gone (e.g. closed externally) — leave music_playing_ as-is, harmless
    if (std::string(status) == "stopped") {
        mciSendStringA("play bgmusic from 0", nullptr, 0, nullptr);
        Logger::Get().Log(LogLevel::DEBUG, "[Music] Looping back to start");
    }
}

void App::StopLevelMusic() {
    if (!music_playing_) return;
    mciSendStringA("close bgmusic", nullptr, 0, nullptr);
    music_playing_ = false;
    Logger::Get().Log(LogLevel::INFO, "[Music] Stopped");
}

void App::ToggleMusic() {
    if (music_playing_) {
        StopLevelMusic();
        Config::Get().musicEnabled = false;
        Config::Save();
        Logger::Get().Log(LogLevel::INFO, "[Music] Toggled OFF via Escape menu");
    } else {
        int lvl = (last_loaded_level_ >= 0) ? last_loaded_level_ : 1;
        PlayLevelMusic(lvl);
        Config::Get().musicEnabled = true;
        Config::Save();
        Logger::Get().Log(LogLevel::INFO, "[Music] Toggled ON via Escape menu (level " + std::to_string(lvl) + ")");
    }
}

void App::ToggleLightmaps() {
    bool on = !Config::Get().enableLightmaps;
    Config::Get().enableLightmaps = on;
    Config::Save();
    renderer_.SetLightmapsEnabled(on);
    Logger::Get().Log(LogLevel::INFO, std::string("[Lightmap] Toggled ") +
        (on ? "ON" : "OFF") + " via Escape menu");
    if (on) {
        // ON: bulk-calculate every Building/EditRigidObj's baked lightmap.
        CalculateLightmapsForAllObjects();
    } else {
        // OFF: drop all baked lightmaps so nothing is shown. Individual objects
        // can still be re-lit any time via the per-object Calculate button.
        renderer_.ClearAllLightmaps();
        Logger::Get().Log(LogLevel::INFO, "[Lightmap] Cleared all baked lightmaps (checkbox OFF)");
    }
}

void App::LoadLevel(int level_no) {
	try {
		Logger::Get().Log(LogLevel::INFO, "[App] ==========================================");
		Logger::Get().Log(LogLevel::INFO, "[App] LoadLevel() START for level " + std::to_string(level_no));
		Logger::Get().Log(LogLevel::INFO, "[App] ==========================================");

		// Track in-session level switches. The FIRST load (last_loaded_level_ < 0) is
		// equivalent to a fresh process; any later load is a MENU/RELOAD switch whose
		// only difference from a fresh process is leftover per-level state.
		const bool is_switch = (last_loaded_level_ >= 0 && last_loaded_level_ != level_no);
		if (is_switch) {
			Logger::Get().Log(LogLevel::INFO, "[App] MENU/RELOAD switch from level " +
				std::to_string(last_loaded_level_) + " to " + std::to_string(level_no) +
				" — performing full previous-level teardown");
			AssetExtractor::ClearLevelAssets(last_loaded_level_, Utils::GetExeDirectory());
		} else {
			Logger::Get().Log(LogLevel::INFO, "[App] Initial level load (level " +
				std::to_string(level_no) + ")");
		}

		AssetExtractor::ClearLevelAssets(level_no, Utils::GetExeDirectory());

		// Verify level number is valid
		if (level_no < MIN_LEVEL_NO || level_no > MAX_LEVEL_NO) {
			std::string errorMsg = "Invalid level number: " + std::to_string(level_no) + " (valid range: " + std::to_string(MIN_LEVEL_NO) + "-" + std::to_string(MAX_LEVEL_NO) + ")";
			Logger::Get().Log(LogLevel::ERR, errorMsg);
			throw std::runtime_error(errorMsg);
		}
	// Always create a pristine full-folder backup of the level on first load.
	// Reset Level (pause menu) restores from this backup, so it must capture the
	// ENTIRE level folder — objects, ai, graphs, models, textures, terrain —
	// before any edits are made. Only create it once per level (if it doesn't
	// exist); subsequent loads reuse the existing backup.
	{
		std::string gameLevelDir = Utils::GetIGIRootPath() + "\\missions\\location0\\level" + std::to_string(level_no);
		std::string backupLevelDir = Utils::GetExeDirectory() + "\\editor\\backup\\level" + std::to_string(level_no);
		if (!std::filesystem::exists(backupLevelDir) && std::filesystem::exists(gameLevelDir)) {
			try {
				std::filesystem::create_directories(backupLevelDir);
				std::filesystem::copy(gameLevelDir, backupLevelDir, std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing);
				Logger::Get().Log(LogLevel::INFO, "[App] Backup created for level " + std::to_string(level_no) + " at " + backupLevelDir);
			} catch (const std::exception& e) {
				Logger::Get().Log(LogLevel::ERR, "[App] Failed to create level backup: " + std::string(e.what()));
			}
		}
	}
		
		selected_object_index_ = -1;
		hover_object_index_ = -1;
		status_message_.clear();

		// NOTE: We must NOT purge the previous level's extracted assets here. The
		// texture/model resolver (FindTextureFile step 6 + lazy cross-level extraction)
		// deliberately searches *other* levels' extracted folders to resolve cross-level
		// references (e.g. a level-6 object using a level-14 texture). Deleting them on
		// switch removes legitimate fallback sources. The renderer caches are still fully
		// torn down by BeginLoadLevel()/ClearCaches() below.
		renderer_.SetLevel(level_no);
		renderer_.BeginLoadLevel();
		Logger::Get().Log(LogLevel::INFO, "[App] After BeginLoadLevel teardown: renderer meshCache=" +
			std::to_string(renderer_.GetMeshCacheCount()) + " textureCache=" +
			std::to_string(renderer_.GetTextureCacheCount()) + " (both should be 0)");
		renderer_.SetSplineTerrainQuery([this](double x, double y, float& z) {
			return level_.GetTerrainZ(x, y, z);
		});

		Level::load_params_s level_load_params_s = {
			.level_no_ = level_no,
			.render_res_loader_ = &renderer_
		};

		// ── Loading overlay ──────────────────────────────────────────────────
		// Staged progress: the load is synchronous, so we redraw + swap the bar at
		// each milestone (10% → 100%). Reusable lambda fills proportionally to pct.
		auto drawLoadBar = [&](int pct, const char* stage) {
			char title[32];
			snprintf(title, sizeof(title), "Loading Level %d", level_no);
			DrawProgressOverlay(title, pct, stage);
		};
		drawLoadBar(10, "reading level");
		// ─────────────────────────────────────────────────────────────────────

		drawLoadBar(25, "models & terrain");
		glm::vec3 start_pos;
		float start_yaw;
		if (level_.Load(level_load_params_s, start_pos, start_yaw)) {
			const auto& config = Config::Get();
			viewer_.pos_ = (config.cameraPosX != 0.0f || config.cameraPosY != 0.0f || config.cameraPosZ != 0.0f) ?
				glm::vec3(config.cameraPosX, config.cameraPosY, config.cameraPosZ) : start_pos;

			bool hasConfigOri = (config.cameraOriX != 0.0f || config.cameraOriY != 0.0f || config.cameraOriZ != 0.0f);
			if (hasConfigOri) {
				viewer_.yaw_   = config.cameraOriX;
				viewer_.pitch_ = config.cameraOriY;
				viewer_.roll_  = config.cameraOriZ;
			} else {
				// Convert game yaw (radians) to viewer yaw (degrees, 0=+Y north, CW).
				// Add 180° so the editor camera faces toward objects rather than with them.
				viewer_.yaw_   = glm::degrees(atan2f(-cosf(start_yaw), sinf(start_yaw))) + 180.0f;
				viewer_.pitch_ = 10.0f;
				viewer_.roll_  = 0.0f;
			}

			UpdateViewerVectors();
			Logger::Get().Log(LogLevel::INFO, "[App] Level " + std::to_string(level_no) + " loaded. Viewer start=(" + std::to_string(viewer_.pos_.x) + "," + std::to_string(viewer_.pos_.y) + "," + std::to_string(viewer_.pos_.z) + ") yaw=" + std::to_string(viewer_.yaw_));
			last_loaded_level_ = level_no;

		}
		else {
			std::string errorMsg = "Failed to load level " + std::to_string(level_no) + "\n\nPlease check if the terrain files exist in the correct location.";
			Utils::ShowError(errorMsg, "IGI Editor - Error");
			Logger::Get().Log(LogLevel::ERR, "[App] Failed to load level " + std::to_string(level_no));
		}


		drawLoadBar(60, "tracks");
		// Step 2: Track Evaluation (Dynamic object placement along paths)
		EvaluateTrainTrackPositions();

		drawLoadBar(75, "snapping objects");
		// Step 3: Always snap objects to terrain after any level load
		Logger::Get().Log(LogLevel::INFO, "[App] Step 3: Snapping objects to terrain...");
		SnapObjectsToTerrain();
		drawLoadBar(90, "finalizing");

		// AI rotation override: AI models (HumanSoldier, HumanAI) only have horizontal rotation
		auto& objects = level_.GetLevelObjects().GetObjects();

		// Build runtime parameter schemas from this level's Task_DeclareParameters so
		// the property editor can expose EVERY declared field of any task type.
		TaskSchemaNS::ClearRegisteredSchemas();
		for (const auto& obj : objects) {
			if (obj.type == "Task_DeclareParameters" && obj.argTokens.size() >= 3) {
				std::string typeName = obj.argTokens[0];
				if (typeName.size() >= 2 && typeName.front() == '"' && typeName.back() == '"')
					typeName = typeName.substr(1, typeName.size() - 2);
				TaskSchemaNS::RegisterSchema(typeName, TaskSchemaNS::ParseDeclaration(obj.argTokens));
			}
		}

		for (auto& obj : objects) {
			if (obj.modelId == "000_01_1") continue; // Skip Player Jones
			if (obj.type == "HumanSoldier" || obj.type == "HumanAI" || obj.type.find("AITYPE") == 0) {
				obj.rot.x = 0.0;           // PITCH = 0
				obj.rot.y = 0.0;           // ROLL = 0
				// Preserve existing rotation if it's already set, otherwise default to a full circle
				if (obj.rot.z == 0.0) obj.rot.z = 6.28318;
				Logger::Get().Log(LogLevel::INFO, "[App] Applied AI rotation override (horizontal only) for " + obj.name + " (" + obj.type + ")");
			}
		}

		// Resolve the level's real sun direction/color from its Dirlight/DirlightKeyframe
		// task (Task_DeclareParameters("DirlightKeyframe","Beta","Angle","Gamma","Angle",
		// "Front Color","RGB","Back Color","RGB","Time","Real32")) so dynamic lighting
		// (the fallback for non-lightmapped faces, and for objects whose lightmap bake
		// has gone stale after a move/rotate) matches the game instead of the old
		// hardcoded vec3(0.5,1.0,0.5)/0.6/0.4 guess. World up is Z (VEC3_Z_DIR), so Beta
		// is elevation and Gamma is azimuth around Z. The editor doesn't simulate
		// time-of-day, so among the FIRST Dirlight task's keyframes we pick the one with
		// the SMALLEST "Time" value (not document order) — levels can list a below-
		// horizon/night keyframe first (observed: beta=153 deg, greenish front color,
		// Time=100) alongside a daytime one (beta=30 deg, warm front color, Time=12);
		// picking by lowest Time consistently lands on the daytime keyframe instead of
		// tinting every object green.
		{
			glm::vec3 sunDir(0.5f, 1.0f, 0.5f);
			glm::vec3 sunFront(0.6f, 0.6f, 0.6f);
			glm::vec3 sunBack(0.4f, 0.4f, 0.4f);
			for (const auto& dl : objects) {
				if (dl.type != "Dirlight") continue;
				bool resolved = false;
				double bestTime = std::numeric_limits<double>::max();
				for (int ci : dl.childrenIndices) {
					if (ci < 0 || ci >= (int)objects.size()) continue;
					const auto& kf = objects[ci];
					if (kf.type != "DirlightKeyframe" || kf.argTokens.size() < 12) continue;
					try {
						double time = std::stod(kf.argTokens[11]);
						if (resolved && time >= bestTime) continue;
						double beta  = std::stod(kf.argTokens[3]);
						double gamma = std::stod(kf.argTokens[4]);
						glm::vec3 front(std::stof(kf.argTokens[5]), std::stof(kf.argTokens[6]), std::stof(kf.argTokens[7]));
						glm::vec3 back (std::stof(kf.argTokens[8]), std::stof(kf.argTokens[9]), std::stof(kf.argTokens[10]));
						// Beta is the angle FROM THE ZENITH (straight up), not from the
						// horizon — beta=30 deg observed in this level's daytime keyframe
						// must mean a near-overhead sun (60 deg elevation) for roofs to be
						// brightly lit like they are in-game. Treating beta as elevation-
						// from-horizon instead (sin(beta) on the up axis) under-lit every
						// upward-facing normal — roofs looked dark in the editor that
						// weren't dark in-game. cos(beta) on the up axis fixes that.
						glm::vec3 dir = glm::normalize(glm::vec3(
							std::sin(beta) * std::cos(gamma),
							std::sin(beta) * std::sin(gamma),
							std::cos(beta)));
						bestTime = time;
						sunFront = front;
						sunBack = back;
						sunDir = dir;
						Logger::Get().Log(LogLevel::INFO, "[App] Dirlight '" +
							(dl.name.empty() ? dl.taskId : dl.name) + "' keyframe time=" + std::to_string(time) +
							" beta=" + std::to_string(beta) + " gamma=" + std::to_string(gamma) +
							" dir=(" + std::to_string(sunDir.x) + "," + std::to_string(sunDir.y) + "," + std::to_string(sunDir.z) + ")" +
							" front=(" + std::to_string(sunFront.r) + "," + std::to_string(sunFront.g) + "," + std::to_string(sunFront.b) + ")");
						resolved = true;
					} catch (const std::exception& e) {
						Logger::Get().Log(LogLevel::WARNING, std::string("[App] DirlightKeyframe args unparsable (") + e.what() + "), using default sun");
					}
				}
				if (resolved) break; // first Dirlight task only
			}
			renderer_.SetSunLight(sunDir, sunFront, sunBack);
		}

		// The level's GlobalLight task (Task_DeclareParameters("GlobalLight",
		// "Radiosity intensity","Real32","Texture filter ambient colour","RGB",
		// "Texture filter scale","RGB","Texture filter gamma","Real32")) declares a
		// post-lighting gamma curve the game applies to every lit object (observed
		// 0.675 in level1) that this editor never applied — our raw linear lighting
		// looks flatter/darker than in-game without it. argTokens: [0]=taskId,
		// [1]="GlobalLight", [2]=name, [3]=Radiosity, [4-6]=ambient colour RGB,
		// [7-9]=scale RGB, [10]=gamma.
		{
			float gamma = 1.0f;
			glm::vec3 globalAmbient(0.15f, 0.15f, 0.15f);
			for (const auto& gl : objects) {
				if (gl.type == "GlobalLight" && gl.argTokens.size() >= 11) {
					try {
						gamma = std::stof(gl.argTokens[10]);
						Logger::Get().Log(LogLevel::INFO, "[App] GlobalLight gamma resolved: " + std::to_string(gamma));
					} catch (const std::exception& e) {
						Logger::Get().Log(LogLevel::WARNING, std::string("[App] GlobalLight gamma unparsable (") + e.what() + "), using defaults");
					}
				}
				if (gl.type == "GlobalLight") {
					for (int ci : gl.childrenIndices) {
						if (ci < 0 || ci >= (int)objects.size()) continue;
						const auto& kf = objects[ci];
						if (kf.type == "GlobalLightKeyframe" && kf.argTokens.size() >= 15) {
							try {
								globalAmbient.r = std::stof(kf.argTokens[12]); // Ambient color object category 1
								globalAmbient.g = std::stof(kf.argTokens[13]);
								globalAmbient.b = std::stof(kf.argTokens[14]);
								Logger::Get().Log(LogLevel::INFO, "[App] GlobalLightKeyframe cat 1: " + std::to_string(globalAmbient.r) + "," + std::to_string(globalAmbient.g) + "," + std::to_string(globalAmbient.b));
								if (kf.argTokens.size() >= 24) {
									Logger::Get().Log(LogLevel::INFO, "[App] cat 2: " + kf.argTokens[15] + "," + kf.argTokens[16] + "," + kf.argTokens[17]);
									Logger::Get().Log(LogLevel::INFO, "[App] cat 3: " + kf.argTokens[18] + "," + kf.argTokens[19] + "," + kf.argTokens[20]);
									Logger::Get().Log(LogLevel::INFO, "[App] cat 4: " + kf.argTokens[21] + "," + kf.argTokens[22] + "," + kf.argTokens[23]);
								}
							} catch (const std::exception& e) {
								Logger::Get().Log(LogLevel::WARNING, std::string("[App] GlobalLightKeyframe ambient unparsable (") + e.what() + "), using defaults");
							}
							break;
						}
					}
					break;
				}
			}
			renderer_.SetGlobalGamma(gamma);
			renderer_.SetGlobalAmbient(globalAmbient);
		}

		// RainEffect (Task_DeclareParameters("RainEffect","Is Rain","bool8",
		// "Traceline start","Real32","Traceline end","Real32","Is Active","VarString",
		// "Rain Alpha","Real32")) is per-level: present+active on levels with rain
		// (e.g. level3), entirely absent on levels without it (e.g. level2) — absence
		// just means no rain, not a parse failure. argTokens: [0]=taskId,
		// [1]="RainEffect",[2]=name,[3]=IsRain(BOOL "TRUE"/"FALSE"),
		// [4]=TracelineStart(meters),[5]=TracelineEnd(meters),
		// [6]=IsActive(quoted VarString "0"/"1"),[7]=RainAlpha.
		{
			bool rainActive = false;
			float rainStartM = 0.0f, rainEndM = 0.0f, rainAlpha = 0.0f;
			for (const auto& re : objects) {
				if (re.type != "RainEffect" || re.argTokens.size() < 8) continue;
				try {
					bool isRain = (re.argTokens[3] == "TRUE");
					std::string isActiveTok = re.argTokens[6];
					if (isActiveTok.size() >= 2 && isActiveTok.front() == '"' && isActiveTok.back() == '"')
						isActiveTok = isActiveTok.substr(1, isActiveTok.size() - 2);
					bool isActive = !isActiveTok.empty() && isActiveTok != "0";
					rainStartM = std::stof(re.argTokens[4]);
					rainEndM = std::stof(re.argTokens[5]);
					rainAlpha = std::stof(re.argTokens[7]);
					rainActive = isRain && isActive;
					Logger::Get().Log(LogLevel::INFO, "[App] RainEffect resolved: active=" +
						std::to_string(rainActive) + " start=" + std::to_string(rainStartM) +
						"m end=" + std::to_string(rainEndM) + "m alpha=" + std::to_string(rainAlpha));
				} catch (const std::exception& e) {
					Logger::Get().Log(LogLevel::WARNING, std::string("[App] RainEffect unparsable (") + e.what() + ")");
				}
				break; // first RainEffect task only
			}
			renderer_.SetRainEffect(rainActive, rainStartM, rainEndM, rainAlpha);
		}

		// Log all loaded objects for verification script
		for (const auto& obj : objects) {
			if (obj.isSplineWaypoint || !obj.segmentModelId.empty()) {
			    Logger::Get().Log(LogLevel::INFO, "[App_Debug] Found waypoint/segment. modelId=" + obj.modelId + " segmentModelId=" + obj.segmentModelId);
			}
			std::string mId = !obj.modelId.empty() ? obj.modelId : obj.segmentModelId;
			if (obj.deleted || mId.empty()) continue;
			Logger::Get().Log(LogLevel::INFO, "[LevelLoader] Object Loaded: ModelID=" + mId +
				", Type=" + obj.type + ", Name=" + obj.name + ", Pos=(" +
				std::to_string(obj.pos.x) + ", " + std::to_string(obj.pos.y) + ", " + std::to_string(obj.pos.z) + ")" +
				", Ori=(" + std::to_string(obj.rot.x) + ", " + std::to_string(obj.rot.y) + ", " + std::to_string(obj.rot.z) + ")" +
				", Tex=" + mId + ", Model=" + mId);		}
		
		RebuildLevelModelIds();

		// Build the set of models packed in this level's .res (names only — stream so we
		// never buffer the whole 200+MB archive in the 32-bit process). (issue 2)
		{
			std::string gameRes = Utils::GetIGIRootPath() +
				"\\missions\\location0\\level" + std::to_string(level_no) +
				"\\models\\level" + std::to_string(level_no) + ".res";
			level_res_models_ = ResModelSet();
			std::string resErr;
			size_t entryCount = 0;
			bool ok = RES_ForEachEntry(gameRes,
				[&](const std::string& name, const uint8_t*, size_t) {
					level_res_models_.AddEntry(name);
					++entryCount;
				}, resErr);
			Logger::Get().Log(LogLevel::INFO, std::string("[App] Level .res model set: ") +
				(ok ? std::to_string(entryCount) + " entries (streamed)"
				    : "UNAVAILABLE (" + gameRes + "): " + resErr));
		}
		// ── After all objects loaded: auto-import animations for HumanSoldier-family NPCs ──
		// Every eligible AI auto-plays its real animation (Stand Animation, AI script,
		// or PatrolPath command) in parallel from the start — not just the one being
		// inspected. Resolving each AI's animation is heavy (it can spawn igi1conv.exe
		// to decompile that AI's .qvm script), so resolution runs on worker threads;
		// only the result merge touches shared state, and only on this (main) thread.
		Logger::Get().Log(LogLevel::INFO, "[App] Auto-importing animations for AI NPCs...");
		animPlaybacks_.clear();
		animIdsCache_.clear();

		std::vector<int> eligible;
		for (int i = 0; i < (int)objects.size(); ++i) {
			if (objects[i].deleted || objects[i].boneHierarchy < 0) continue;
			eligible.push_back(i);
		}

		// Phase 1 (sequential, main thread): import every distinct bone hierarchy's
		// animation set BEFORE any worker thread touches AnimationRegistry — its
		// internal cache map is written lazily on first import and is not safe to
		// write concurrently for the same or different bone hierarchies.
		std::set<int> hierarchiesToImport;
		for (int i : eligible) hierarchiesToImport.insert(objects[i].boneHierarchy);
		for (int h : hierarchiesToImport) animRegistry_.ImportAnimations(h);

		// Phase 2 (parallel, worker threads): resolve each AI's animation ids + first
		// matching clip. Read-only against AnimationRegistry (already fully imported
		// above) and LevelObjects; writes only to each task's own local result.
		struct AnimResolveResult { int objIndex; std::vector<int> ids; const AnimationClip* clip = nullptr; bool usedDefault = false; };
		std::vector<std::future<AnimResolveResult>> futures;
		futures.reserve(eligible.size());
		for (int i : eligible) {
			futures.push_back(std::async(std::launch::async, [this, i]() -> AnimResolveResult {
				AnimResolveResult r{i};
				r.ids = ComputeAnimationIdsForObject(i);
				const auto& o = level_.GetLevelObjects().GetObjects()[i];
				for (int id : r.ids) {
					r.clip = animRegistry_.GetClipByAnimId(o.boneHierarchy, id);
					if (r.clip) break;
				}
				// No specific referenced animation resolved -> fall back to the bone
				// hierarchy's default (lowest-animId) clip so EVERY AI animates by
				// default, not just those whose QSC/AI-script names an animation.
				if (!r.clip) {
					r.clip = animRegistry_.GetDefaultClip(o.boneHierarchy);
					r.usedDefault = (r.clip != nullptr);
				}
				return r;
			}));
		}
		Logger::Get().Log(LogLevel::INFO, "[Anim] Resolving animations for " +
			std::to_string(eligible.size()) + " AI NPC(s) across " +
			std::to_string(futures.size()) + " worker thread(s) in parallel...");

		// Phase 3 (sequential, main thread): merge results — animPlaybacks_ /
		// animIdsCache_ are plain maps, never written from worker threads directly,
		// so there is no clash here even though Phase 2 ran fully concurrently.
		for (auto& f : futures) {
			AnimResolveResult r = f.get();
			animIdsCache_[r.objIndex] = r.ids;
			if (!r.clip) continue;
			AnimPlayback pb;
			pb.Start(r.clip);
			pb.forceLoop = true;  // every AI animates continuously, not just once
			animPlaybacks_[r.objIndex] = pb;
			Logger::Get().Log(LogLevel::INFO, "[Anim] Auto-playing '" + r.clip->name + "'" +
				(r.usedDefault ? " (DEFAULT fallback — no referenced animation)" : "") +
				" for object " + std::to_string(r.objIndex) + " (" + objects[r.objIndex].type + ")");
		}
		Logger::Get().Log(LogLevel::INFO, "[App] Animation init complete: " +
			std::to_string(animPlaybacks_.size()) + " of " + std::to_string(eligible.size()) +
			" eligible AI NPCs have a real animation and are now playing in parallel.");

		Logger::Get().Log(LogLevel::INFO, "[App] ==========================================");
		Logger::Get().Log(LogLevel::INFO, "[App] LoadLevel() COMPLETE for level " + std::to_string(level_no));
		Logger::Get().Log(LogLevel::INFO, "[App] ==========================================");
		drawLoadBar(100, "done");

		if (Config::Get().musicEnabled) PlayLevelMusic(level_no);

		// Auto-load all baked lightmaps on every level start (whether or not the
		// Escape-menu checkbox is on) so the level looks lit by default, like the
		// game. The checkbox is a runtime toggle: OFF clears them (default bright
		// look), ON re-calculates. Only runs when the level ships a lightmaps.res.
		{
			const std::string lmRes = Utils::GetIGIRootPath() + "\\missions\\location0\\level" +
				std::to_string(level_no) + "\\lightmaps\\lightmaps.res";
			if (std::filesystem::exists(lmRes)) {
				Logger::Get().Log(LogLevel::INFO, "[Lightmap] Auto-loading baked lightmaps for level " + std::to_string(level_no));
				CalculateLightmapsForAllObjects();
			}
		}
	}
	catch (const std::exception& e) {
		std::string errorMsg = "Error loading level " + std::to_string(level_no) + ":\n" + std::string(e.what());
		Utils::LogAndShowError(errorMsg, "IGI Editor - Error");
		Logger::Get().Log(LogLevel::ERR, errorMsg);
	}
	catch (...) {
		std::string errorMsg = "Unknown error loading level " + std::to_string(level_no);
		Utils::LogAndShowError(errorMsg, "IGI Editor - Error");
		Logger::Get().Log(LogLevel::ERR, errorMsg);
	}
}

void App::WriteBackRecalculatedLightmaps() {
	const std::string levelDir = Utils::GetIGIRootPath() + "\\missions\\location0\\level" +
		std::to_string(level_.GetLevelNo());
	const std::string lightmapsDir = levelDir + "\\lightmaps";
	const std::string unpackedDir = lightmapsDir + "\\lightmaps_unpacked";
	const std::string resPath = lightmapsDir + "\\lightmaps.res";
	if (!std::filesystem::exists(resPath)) return;

	// Find lightmapped objects that were moved/rotated since their bake.
	auto& objects = level_.GetLevelObjects().GetObjects();
	std::vector<int> moved;
	for (int i = 0; i < (int)objects.size(); ++i) {
		const auto& o = objects[i];
		if (o.type != "Building" && o.type != "EditRigidObj") continue;
		if (o.taskId.empty() || o.taskId == "-1") continue;
		if (!renderer_.HasLightmapForTask(LightmapTaskKey(o))) continue;
		if (renderer_.IsLightmapStale(LightmapTaskKey(o), o.pos, o.rot)) moved.push_back(i);
	}
	if (moved.empty()) return; // nothing to bake into the game

	std::string unpackErr;
	if (!EnsureLightmapsUnpacked(levelDir, unpackErr)) {
		Logger::Get().Log(LogLevel::WARNING, "[Lightmap] Write-back: unpack failed: " + unpackErr);
		return;
	}
	const std::string qvmPath = levelDir + "\\objects.qvm";
	const std::string qscPath = levelDir + "\\objects_lightmap_tmp.qsc";
	std::string decompileErr;
	if (!igi1conv::QvmDecompile(qvmPath, qscPath, decompileErr)) {
		Logger::Get().Log(LogLevel::WARNING, "[Lightmap] Write-back: decompile failed: " + decompileErr);
		return;
	}

	Logger::Get().Log(LogLevel::INFO, "[Lightmap] Write-back: re-lighting " +
		std::to_string(moved.size()) + " moved object(s) into .olm");
	int baked = 0;
	for (size_t n = 0; n < moved.size(); ++n) {
		DrawProgressOverlay("Saving lightmaps", static_cast<int>(90 * (n + 1) / moved.size()),
			"re-lighting moved objects");
		if (RecalcLightmapToOlm(objects[moved[n]], qscPath)) ++baked;
	}
	std::error_code qscEc; std::filesystem::remove(qscPath, qscEc);
	if (baked == 0) return;

	// Name-preserving repack into a temp .res, then atomically swap it in.
	// Pristine lightmaps.res is in the per-level backup (Reset Level restores it).
	const std::string tmpRes = lightmapsDir + "\\lightmaps_new.res";
	std::string err;
	if (!igi1conv::ResRepack(resPath, unpackedDir, tmpRes, err)) {
		Logger::Get().Log(LogLevel::ERR, "[Lightmap] res repack failed: " + err);
		std::error_code ec; std::filesystem::remove(tmpRes, ec);
		return;
	}
	std::error_code ec;
	std::filesystem::rename(tmpRes, resPath, ec);
	if (ec) {
		std::filesystem::copy_file(tmpRes, resPath, std::filesystem::copy_options::overwrite_existing, ec);
		std::error_code ec2; std::filesystem::remove(tmpRes, ec2);
	}
	if (ec) {
		Logger::Get().Log(LogLevel::ERR, "[Lightmap] Could not replace " + resPath + ": " + ec.message());
		return;
	}
	Logger::Get().Log(LogLevel::INFO, "[Lightmap] Wrote " + std::to_string(baked) +
		" re-lit lightmap(s) into " + resPath + " — game will show the new lighting");
	status_message_ += "  |  lightmaps written to game (" + std::to_string(baked) + " obj)";
}

void App::SetGameLevel(int level_no) {
	bridge_.SetGameLevel(level_no);
}








void App::FlushAttaProxiesToMef() {
	for (auto& obj : level_.GetLevelObjects().GetObjects()) {
		if (!obj.isAttaProxy || !obj.modified) continue;

		// Position: convert world pos → local pos via stored inverse parent matrix.
		glm::vec4 lp = obj.attaInvParentMat * glm::vec4(glm::vec3(obj.pos), 1.0f);
		glm::vec3 localPos(lp);

		// Rotation: rebuild world rotation matrix from Euler angles (Rz * Rx * Ry),
		// then convert to local rotation: localRot = parentRotInv * worldRot.
		// The upper-left 3×3 of attaInvParentMat IS parentRotInv.
		glm::mat4 wr(1.0f);
		wr = glm::rotate(wr, (float)obj.rot.z, glm::vec3(0,0,1));
		wr = glm::rotate(wr, (float)obj.rot.x, glm::vec3(1,0,0));
		wr = glm::rotate(wr, (float)obj.rot.y, glm::vec3(0,1,0));
		glm::mat3 worldRot3(wr);
		glm::mat3 parentRotInv = glm::mat3(obj.attaInvParentMat);
		glm::mat3 localRot = parentRotInv * worldRot3;

		renderer_.UpdateAttaLocalPosInMef(obj.attaParentModelId, obj.attaIsBuilding,
		                                  obj.attaRecordIndex, localPos, localRot);
		obj.modified = false;
	}
}

void App::SaveCurrentLevel() {
	try {
		Logger::Get().Log(LogLevel::INFO, "[App] SaveCurrentLevel() called");

		// Flush any in-flight property-panel edit (especially the AI script text
		// editor) before serializing. Otherwise pressing SaveState while the AI
		// script box still has focus would save the stale ai_script_text_ and
		// silently drop the user's edits.
		CommitPropTextEdit();

		// Before serializing objects.qsc, sync the AIGraph task's declared
		// Graphdata counts (node_count = argTokens[7], edge_count = argTokens[9])
		// to the edited graph. The game reads the graph using these counts, so a
		// mismatch (e.g. after deleting nodes) crashes it on load.
		if (renderer_.GraphOverlayDirty() && !renderer_.GraphOverlayTaskId().empty()) {
			auto& objs = level_.GetLevelObjects().GetObjects();
			for (auto& o : objs) {
				if (o.type == "AIGraph" && o.taskId == renderer_.GraphOverlayTaskId() &&
				    o.argTokens.size() > 9) {
					o.argTokens[7] = std::to_string(renderer_.GraphOverlayNodeCount());
					o.argTokens[9] = std::to_string(renderer_.GraphOverlayEdgeCount());
					o.modified = true;
					Logger::Get().Log(LogLevel::INFO,
						"[App] AIGraph " + o.taskId + " counts synced: nodes=" +
						o.argTokens[7] + " edges=" + o.argTokens[9]);
					break;
				}
			}
		}

		FlushAttaProxiesToMef();
		level_.SaveChanges();

		// Persist edited navigation-graph nodes back to graph<taskId>.dat.
		if (renderer_.GraphOverlayDirty()) {
			if (renderer_.SaveGraphOverlay())
				Logger::Get().Log(LogLevel::INFO, "[App] Graph overlay saved with level");
			else
				Logger::Get().Log(LogLevel::ERR, "[App] Graph overlay save FAILED");
		}

		// Compile edited AI script (.qsc text -> .qvm file) before saving the level QVM.
		if (ai_script_dirty_ && !ai_script_path_.empty()) {
			Logger::Get().Log(LogLevel::INFO, "[App] Compiling modified AI script to: " + ai_script_path_);
			auto lexResult   = qsc::Lex(ai_script_text_);
			auto parseResult = lexResult.ok ? qsc::Parse(lexResult.tokens) : qsc::ParseResult{};
			std::string compileErr;
			bool ok = lexResult.ok && parseResult.ok &&
			          qvm::CompileToFile(*parseResult.program, ai_script_path_, &compileErr);
			if (ok) {
				QVMFile check = QVM_Parse(ai_script_path_);
				if (check.valid) {
					ai_script_dirty_ = false;
					status_message_ = "AI script compiled: " + ai_script_path_;
					Logger::Get().Log(LogLevel::INFO, "[App] AI script compiled OK");
				} else {
					Logger::Get().Log(LogLevel::ERR, "[App] AI script round-trip failed -- file may be corrupt");
					status_message_ = "AI script compile: round-trip failed";
				}
			} else {
				std::string detail = compileErr.empty() ? "(no detail)" : compileErr;
				Logger::Get().Log(LogLevel::ERR, "[App] AI script compile error: " + detail);
				status_message_ = "AI script compile error: " + detail;
			}
		}

		Logger::Get().Log(LogLevel::INFO, "[App] Calling SaveAndCompile()");
		SaveAndCompile();

		// Repack any recalculated lightmaps back into the level's lightmaps.res so
		// the GAME (igi.exe) shows the new lighting, not the original prebaked one.
		WriteBackRecalculatedLightmaps();
	}
	catch (const std::exception& e) {
		std::string errorMsg = "Error saving level:\n" + std::string(e.what());
		Utils::LogAndShowError(errorMsg, "IGI Editor - Error");
		Logger::Get().Log(LogLevel::ERR, errorMsg);
	}
	catch (...) {
		std::string errorMsg = "Unknown error saving level";
		Utils::LogAndShowError(errorMsg, "IGI Editor - Error");
		Logger::Get().Log(LogLevel::ERR, errorMsg);
	}
}

void App::ExportTextureMap() {
	int levelNo = level_.GetLevelNo();
	const std::string root = Utils::GetIGIRootPath();
	const std::string datPath = root + "\\missions\\location0\\level" +
	    std::to_string(levelNo) + "\\level" + std::to_string(levelNo) + ".dat";
	const std::string outPath = Utils::GetExeDirectory() +
	    "\\level" + std::to_string(levelNo) + "_texmap.json";

	Logger::Get().Log(LogLevel::INFO,
	    "[App] ExportTextureMap level=" + std::to_string(levelNo) +
	    " dat=" + datPath + " out=" + outPath);

	DATFile dat = DAT_Parse(datPath);
	if (!dat.valid) {
		Utils::ShowError("Failed to parse DAT:\n" + dat.error, "Export Texture Map");
		return;
	}
	if (DAT_WriteJSON(dat, outPath)) {
		Utils::ShowInfo(
		    "Texture map exported (JSON):\n" + outPath +
		    "\nModels: " + std::to_string(dat.models.size()) +
		    "  Textures: " + std::to_string(dat.allTextures.size()),
		    "Export Texture Map");
	} else {
		Utils::ShowError("Could not write to:\n" + outPath, "Export Texture Map");
	}
}

