// The options menu's update check: BlackActivity asks GitHub and shows the answer in a dialog of its own
#include <atomic>

#include <jni.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_system.h>

namespace host {

// a method of the activity; any thread, SDL attaches it to the VM. a missing method leaves an exception pending,
// which aborts the app at the next JNI call unless cleared
template <typename F>
static void WithActivity(const char* name, const char* sig, F&& call) {
    auto* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
    auto activity = static_cast<jobject>(SDL_GetAndroidActivity());
    if (!env || !activity) return;
    jclass cls = env->GetObjectClass(activity);
    jmethodID m = env->GetMethodID(cls, name, sig);
    if (env->ExceptionCheck()) env->ExceptionClear();
    if (m) call(env, activity, m);
    if (env->ExceptionCheck()) env->ExceptionClear();
    env->DeleteLocalRef(cls);
    env->DeleteLocalRef(activity);
}

void CheckForUpdates() {
    WithActivity("checkForUpdates", "()V", [](JNIEnv* env, jobject a, jmethodID m) { env->CallVoidMethod(a, m); });
}

// asked once, then kept here: the menu reads it every frame
static std::atomic<int> g_startup{-1};

bool StartupCheck() {
    int v = g_startup.load();
    if (v < 0) {
        v = 1;
        WithActivity("isStartupCheckEnabled", "()Z",
                     [&](JNIEnv* env, jobject a, jmethodID m) { v = env->CallBooleanMethod(a, m) ? 1 : 0; });
        g_startup = v;
    }
    return v != 0;
}

void SetStartupCheck(bool on) {
    g_startup = on ? 1 : 0;
    WithActivity("setStartupCheckEnabled", "(Z)V",
                 [&](JNIEnv* env, jobject a, jmethodID m) { env->CallVoidMethod(a, m, jboolean(on)); });
}

}  // namespace host
