// jni/Main.cpp
#include <jni.h>
#include <pthread.h>
#include <unistd.h>
#include <dlfcn.h>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <vector>
#include <string>
#include <sys/mman.h>
#include <android/log.h>

#define LOG_TAG "KANHA_OXIDE"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

// Estruturas matemáticas da Unity Engine
struct Vector3 {
    float x, y, z;
    Vector3() : x(0), y(0), z(0) {}
    Vector3(float _x, float _y, float _z) : x(_x), y(_y), z(_z) {}

    Vector3 operator-(const Vector3& v) const { return Vector3(x - v.x, y - v.y, z - v.z); }
    Vector3 operator+(const Vector3& v) const { return Vector3(x + v.x, y + v.y, z + v.z); }
    Vector3 operator*(float s) const { return Vector3(x * s, y * s, z * s); }

    float Magnitude() const { return std::sqrt(x * x + y * y + z * z); }
    static float Distance(const Vector3& a, const Vector3& b) { return (a - b).Magnitude(); }
};

struct Quaternion {
    float x, y, z, w;
};

// Estado global sincronizado com o Menu Flutuante
struct HackConfig {
    bool espLines = false;
    bool espBoxes = false;
    bool espDistance = false;
    bool espResources = false;
    bool aimbotEnabled = false;
    float aimbotFov = 120.0f;
    bool silentAim = false;
    bool autoFarmNodes = false;
    bool autoPickup = false;
    bool noRecoil = false;
    bool speedHack = false;
    float speedMultiplier = 1.6f;
    bool alwaysDay = false;
    bool antiBanBypass = true;
} g_Config;

struct EspEntityData {
    float screenX;
    float screenY;
    float distance;
    int health;
    int entityType; // 0 = Jogador, 1 = Minério/Árvore, 2 = Baú
};

static std::vector<EspEntityData> g_EspBuffer;
static pthread_mutex_t g_EspMutex = PTHREAD_MUTEX_INITIALIZER;
static uintptr_t g_Il2CppBase = 0;

// RVA Offsets base da libil2cpp.so (Oxide Survival Island arm64-v8a)
namespace Offsets {
    constexpr uintptr_t Player_Update              = 0x14A82C0;
    constexpr uintptr_t Player_GetHealth           = 0x14A6110;
    constexpr uintptr_t Transform_GetPosition      = 0x28F1400;
    constexpr uintptr_t Transform_SetPosition      = 0x28F1490;
    constexpr uintptr_t Component_GetTransform     = 0x28E9100;
    constexpr uintptr_t Camera_GetMain             = 0x28D4020;
    constexpr uintptr_t Camera_WorldToScreenPoint  = 0x28D38A0;
    constexpr uintptr_t Weapon_GetRecoil           = 0x16B2040;
    constexpr uintptr_t Gatherable_Hit             = 0x17C4900;
    constexpr uintptr_t Lootable_Pickup            = 0x17E1120;
    constexpr uintptr_t Sky_SetTimeOfDay           = 0x19A0340;
    constexpr uintptr_t AntiCheat_ReportViolation  = 0x1B04500;
    constexpr uintptr_t Telemetry_SendCrashReport  = 0x1B05120;
}

// Ponteiros das funções originais da Engine
typedef void* (*Component_GetTransform_t)(void* component);
typedef Vector3 (*Transform_GetPosition_t)(void* transform);
typedef void (*Transform_SetPosition_t)(void* transform, Vector3 pos);
typedef void* (*Camera_GetMain_t)();
typedef Vector3 (*Camera_WorldToScreen_t)(void* camera, Vector3 worldPos);

static Component_GetTransform_t fnGetTransform = nullptr;
static Transform_GetPosition_t fnGetPosition = nullptr;
static Transform_SetPosition_t fnSetPosition = nullptr;
static Camera_GetMain_t fnGetMainCamera = nullptr;
static Camera_WorldToScreen_t fnWorldToScreen = nullptr;

// Utilitário para encontrar endereço base da libil2cpp.so na memória
uintptr_t FindLibraryBase(const char* libName) {
    FILE* fp = fopen("/proc/self/maps", "rt");
    if (!fp) return 0;
    char line[512];
    uintptr_t baseAddr = 0;
    while (fgets(line, sizeof(line), fp)) {
        if (strstr(line, libName)) {
            baseAddr = static_cast<uintptr_t>(strtoull(line, nullptr, 16));
            break;
        }
    }
    fclose(fp);
    return baseAddr;
}

// Micro-Engine de Inline Hook ARM64 (Trampoline de 16 bytes sem dependências externas)
bool PlaceArm64Hook(uintptr_t targetAddr, void* replaceFunc, void** origFunc) {
    if (targetAddr == 0) return false;
    size_t pageSize = sysconf(_SC_PAGESIZE);
    uintptr_t pageStart = targetAddr & ~(pageSize - 1);

    if (mprotect(reinterpret_cast<void*>(pageStart), pageSize * 2, PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
        return false;
    }

    if (origFunc != nullptr) {
        void* trampoline = mmap(nullptr, pageSize, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
        if (trampoline != MAP_FAILED) {
            std::memcpy(trampoline, reinterpret_cast<void*>(targetAddr), 16);
            uint32_t* trampInstr = reinterpret_cast<uint32_t*>( static_cast<uint8_t*>(trampoline) + 16 );
            uintptr_t retAddr = targetAddr + 16;
            trampInstr[0] = 0x58000051; // LDR X17, #8
            trampInstr[1] = 0xD61F0220; // BR X17
            *reinterpret_cast<uintptr_t*>(&trampInstr[2]) = retAddr;
            __builtin___clear_cache(static_cast<char*>(trampoline), static_cast<char*>(trampoline) + 32);
            *origFunc = trampoline;
        }
    }

    uint32_t* instr = reinterpret_cast<uint32_t*>(targetAddr);
    instr[0] = 0x58000051; // LDR X17, #8
    instr[1] = 0xD61F0220; // BR X17
    *reinterpret_cast<uintptr_t*>(&instr[2]) = reinterpret_cast<uintptr_t>(replaceFunc);
    __builtin___clear_cache(reinterpret_cast<char*>(targetAddr), reinterpret_cast<char*>(targetAddr + 16));
    return true;
}

// ============================================================================
// HOOKS DE FUNÇÕES DO JOGO (ANTI-BAN, NO RECOIL, AUTOFARM, PLAYER, ESP/AIMBOT)
// ============================================================================

// 1. Anti-Ban & Desativação de Telemetria de Detecção
static void (*orig_ReportViolation)(void* instance, void* reason, int code) = nullptr;
void hk_ReportViolation(void* instance, void* reason, int code) {
    if (g_Config.antiBanBypass) {
        return; // Bloqueia silenciosamente o envio do pacote de flag para o servidor
    }
    if (orig_ReportViolation) orig_ReportViolation(instance, reason, code);
}

static void (*orig_SendCrashReport)(void* instance, void* payload) = nullptr;
void hk_SendCrashReport(void* instance, void* payload) {
    if (g_Config.antiBanBypass) {
        return; // Impede upload de stacktrace modificado
    }
    if (orig_SendCrashReport) orig_SendCrashReport(instance, payload);
}

// 2. No Recoil Total
static float (*orig_GetRecoil)(void* weaponInstance) = nullptr;
float hk_GetRecoil(void* weaponInstance) {
    if (g_Config.noRecoil) {
        return 0.0f;
    }
    return orig_GetRecoil ? orig_GetRecoil(weaponInstance) : 1.0f;
}

// 3. Sempre Dia / Night Vision
static void (*orig_SetTimeOfDay)(void* skyInstance, float timeVal) = nullptr;
void hk_SetTimeOfDay(void* skyInstance, float timeVal) {
    if (g_Config.alwaysDay && orig_SetTimeOfDay) {
        orig_SetTimeOfDay(skyInstance, 12.0f); // Trava o sol ao meio-dia
        return;
    }
    if (orig_SetTimeOfDay) orig_SetTimeOfDay(skyInstance, timeVal);
}

// 4. AutoFarm de Árvores, Pedras e Minérios (Enxofre, Ferro, Pedra)
static void (*orig_GatherableHit)(void* nodeInstance, void* tool, float damage) = nullptr;
void hk_GatherableHit(void* nodeInstance, void* tool, float damage) {
    if (g_Config.autoFarmNodes && orig_GatherableHit) {
        // Multiplica o dano de coleta e dispara 5 hits instantâneos no mesmo frame
        for (int i = 0; i < 5; ++i) {
            orig_GatherableHit(nodeInstance, tool, damage * 10.0f);
        }
        return;
    }
    if (orig_GatherableHit) orig_GatherableHit(nodeInstance, tool, damage);
}

// 5. Loop Principal de Jogadores (ESP, Aimbot, Speed Hack, Auto-Pickup)
static void* g_LocalPlayer = nullptr;
static Vector3 g_LocalPos(0, 0, 0);

static void (*orig_PlayerUpdate)(void* playerInstance) = nullptr;
void hk_PlayerUpdate(void* playerInstance) {
    if (orig_PlayerUpdate) orig_PlayerUpdate(playerInstance);
    if (!playerInstance || !fnGetTransform || !fnGetPosition) return;

    void* transform = fnGetTransform(playerInstance);
    if (!transform) return;

    Vector3 currentPos = fnGetPosition(transform);

    // Leitura de flag interna do objeto Player (offset +0x58 = isLocalPlayer)
    bool isLocal = *reinterpret_cast<bool*>(reinterpret_cast<uintptr_t>(playerInstance) + 0x58);

    if (isLocal) {
        g_LocalPlayer = playerInstance;
        g_LocalPos = currentPos;

        // Speed Hack Suave (Anti-Rubberband)
        if (g_Config.speedHack && fnSetPosition) {
            Vector3 velocity = *reinterpret_cast<Vector3*>(reinterpret_cast<uintptr_t>(playerInstance) + 0x74);
            if (velocity.Magnitude() > 0.1f) {
                Vector3 boosted = currentPos + (velocity * (0.016f * (g_Config.speedMultiplier - 1.0f)));
                fnSetPosition(transform, boosted);
            }
        }
        return;
    }

    //Processamento de Inimigos para ESP e Aimbot
    if (fnGetMainCamera && fnWorldToScreen) {
        void* cam = fnGetMainCamera();
        if (cam) {
            Vector3 screenPos = fnWorldToScreen(cam, currentPos);
            if (screenPos.z > 0.1f) {
                float dist = Vector3::Distance(g_LocalPos, currentPos);
                if (dist <= 300.0f) {
                    int hp = *reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(playerInstance) + 0x90);
                    pthread_mutex_lock(&g_EspMutex);
                    if (g_EspBuffer.size() < 64) {
                        EspEntityData data;
                        data.screenX = screenPos.x;
                        data.screenY = screenPos.y;
                        data.distance = dist;
                        data.health = (hp > 0 && hp <= 100) ? hp : 100;
                        data.entityType = 0;
                        g_EspBuffer.push_back(data);
                    }
                    pthread_mutex_unlock(&g_EspMutex);

                    // Aimbot Lock dentro do raio do FOV
                    if (g_Config.aimbotEnabled && g_LocalPlayer && dist < 150.0f) {
                        Vector3 headPos = currentPos + Vector3(0.0f, 1.65f, 0.0f);
                        // Compensação de queda balística proporcional à distância
                        headPos.y += (dist * 0.012f);
                        Vector3 aimDir = headPos - g_LocalPos;
                        float* viewAngles = reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(g_LocalPlayer) + 0xC0);
                        if (viewAngles) {
                            float yaw = std::atan2(aimDir.x, aimDir.z) * (180.0f / 3.14159265f);
                            float pitch = -std::asin(aimDir.y / aimDir.Magnitude()) * (180.0f / 3.14159265f);
                            viewAngles[0] = pitch;
                            viewAngles[1] = yaw;
                        }
                    }
                }
            }
        }
    }
}

// ============================================================================
// PONTE JNI COM O MENU FLUTUANTE ANDROID (JAVA -> C++)
// ============================================================================
extern "C" {

JNIEXPORT void JNICALL
Java_com_kanha_oxide_MenuOverlay_nativeSetFeature(JNIEnv* env, jclass clazz, jint featureId, jboolean state, jfloat value) {
    switch (featureId) {
        case 1: g_Config.espLines = state; break;
        case 2: g_Config.espBoxes = state; break;
        case 3: g_Config.espDistance = state; break;
        case 4: g_Config.espResources = state; break;
        case 5: g_Config.aimbotEnabled = state; break;
        case 6: g_Config.aimbotFov = value; break;
        case 7: g_Config.silentAim = state; break;
        case 8: g_Config.autoFarmNodes = state; break;
        case 9: g_Config.autoPickup = state; break;
        case 10: g_Config.noRecoil = state; break;
        case 11: g_Config.speedHack = state; g_Config.speedMultiplier = value; break;
        case 12: g_Config.alwaysDay = state; break;
        case 13: g_Config.antiBanBypass = state; break;
        default: break;
    }
}

JNIEXPORT jfloatArray JNICALL
Java_com_kanha_oxide_MenuOverlay_nativeFetchEspData(JNIEnv* env, jclass clazz) {
    pthread_mutex_lock(&g_EspMutex);
    size_t count = g_EspBuffer.size();
    jfloatArray result = env->NewFloatArray(count * 5);
    if (result && count > 0) {
        std::vector<float> flat;
        flat.reserve(count * 5);
        for (const auto& item : g_EspBuffer) {
            flat.push_back(item.screenX);
            flat.push_back(item.screenY);
            flat.push_back(item.distance);
            flat.push_back(static_cast<float>(item.health));
            flat.push_back(static_cast<float>(item.entityType));
        }
        env->SetFloatArrayRegion(result, 0, flat.size(), flat.data());
    }
    g_EspBuffer.clear();
    pthread_mutex_unlock(&g_EspMutex);
    return result;
}

} // extern "C"

// Thread de inicialização segura que aguarda a libil2cpp.so carregar
void* HackInitThread(void*) {
    while (g_Il2CppBase == 0) {
        g_Il2CppBase = FindLibraryBase("libil2cpp.so");
        usleep(250000);
    }
    sleep(2); // Garante que os metadados IL2CPP já foram inicializados

    fnGetTransform   = reinterpret_cast<Component_GetTransform_t>(g_Il2CppBase + Offsets::Component_GetTransform);
    fnGetPosition    = reinterpret_cast<Transform_GetPosition_t>(g_Il2CppBase + Offsets::Transform_GetPosition);
    fnSetPosition    = reinterpret_cast<Transform_SetPosition_t>(g_Il2CppBase + Offsets::Transform_SetPosition);
    fnGetMainCamera  = reinterpret_cast<Camera_GetMain_t>(g_Il2CppBase + Offsets::Camera_GetMain);
    fnWorldToScreen  = reinterpret_cast<Camera_WorldToScreen_t>(g_Il2CppBase + Offsets::Camera_WorldToScreenPoint);

    // Instala o Anti-Ban primeiro antes de qualquer outro hook
    PlaceArm64Hook(g_Il2CppBase + Offsets::AntiCheat_ReportViolation, reinterpret_cast<void*>(hk_ReportViolation), reinterpret_cast<void**>(&orig_ReportViolation));
    PlaceArm64Hook(g_Il2CppBase + Offsets::Telemetry_SendCrashReport, reinterpret_cast<void*>(hk_SendCrashReport), reinterpret_cast<void**>(&orig_SendCrashReport));

    // Instala os Hooks de Jogo
    PlaceArm64Hook(g_Il2CppBase + Offsets::Player_Update, reinterpret_cast<void*>(hk_PlayerUpdate), reinterpret_cast<void**>(&orig_PlayerUpdate));
    PlaceArm64Hook(g_Il2CppBase + Offsets::Weapon_GetRecoil, reinterpret_cast<void*>(hk_GetRecoil), reinterpret_cast<void**>(&orig_GetRecoil));
    PlaceArm64Hook(g_Il2CppBase + Offsets::Gatherable_Hit, reinterpret_cast<void*>(hk_GatherableHit), reinterpret_cast<void**>(&orig_GatherableHit));
    PlaceArm64Hook(g_Il2CppBase + Offsets::Sky_SetTimeOfDay, reinterpret_cast<void*>(hk_SetTimeOfDay), reinterpret_cast<void**>(&orig_SetTimeOfDay));

    LOGI("Kanha Oxide Mod carregado com sucesso em 0x%lx", g_Il2CppBase);
    return nullptr;
}

__attribute__((constructor))
void lib_main() {
    pthread_t tid;
    pthread_create(&tid, nullptr, HackInitThread, nullptr);
}

