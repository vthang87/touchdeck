//! ObjC bridge: system Now Playing via private MediaRemote.framework.

#include <Foundation/Foundation.h>
#include <dispatch/dispatch.h>
#include <dlfcn.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef void (*MRMediaRemoteGetNowPlayingInfoFn)(dispatch_queue_t, void (^)(NSDictionary *));
typedef void (*MRMediaRemoteGetNowPlayingApplicationIsPlayingFn)(dispatch_queue_t, void (^)(Boolean));
typedef void (*MRMediaRemoteSetElapsedTimeFn)(double);
typedef void (*MRMediaRemoteSetPlaybackSpeedFn)(int);

static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static char *g_json = NULL;
static MRMediaRemoteSetElapsedTimeFn g_setElapsed = NULL;
static MRMediaRemoteSetPlaybackSpeedFn g_setSpeed = NULL;

static void set_json_locked(const char *s) {
  free(g_json);
  g_json = s ? strdup(s)
             : strdup(
                   "{\"title\":\"\",\"artist\":\"\",\"playing\":false,\"pos_ms\":0,\"dur_ms\":0,\"app\":\"\"}");
}

char *touchdeck_now_playing_json_copy(void) {
  pthread_mutex_lock(&g_mu);
  char *out = g_json ? strdup(g_json)
                     : strdup(
                           "{\"title\":\"\",\"artist\":\"\",\"playing\":false,\"pos_ms\":0,\"dur_ms\":0,\"app\":\"\"}");
  pthread_mutex_unlock(&g_mu);
  return out;
}

void touchdeck_now_playing_free(char *p) { free(p); }

static NSString *escape_json(NSString *in) {
  if (!in) return @"";
  NSMutableString *o = [NSMutableString stringWithString:in];
  [o replaceOccurrencesOfString:@"\\" withString:@"\\\\" options:0 range:NSMakeRange(0, o.length)];
  [o replaceOccurrencesOfString:@"\"" withString:@"\\\"" options:0 range:NSMakeRange(0, o.length)];
  [o replaceOccurrencesOfString:@"\n" withString:@" " options:0 range:NSMakeRange(0, o.length)];
  [o replaceOccurrencesOfString:@"\r" withString:@" " options:0 range:NSMakeRange(0, o.length)];
  return o;
}

static NSString *dict_str(NSDictionary *info, NSArray<NSString *> *keys) {
  for (NSString *k in keys) {
    id v = info[k];
    if ([v isKindOfClass:[NSString class]] && [(NSString *)v length] > 0) {
      return (NSString *)v;
    }
  }
  return @"";
}

static NSNumber *dict_num(NSDictionary *info, NSArray<NSString *> *keys) {
  for (NSString *k in keys) {
    id v = info[k];
    if ([v isKindOfClass:[NSNumber class]]) {
      return (NSNumber *)v;
    }
  }
  return nil;
}

void touchdeck_now_playing_poll_async(void);

/// Set absolute elapsed time (seconds) on the system Now Playing session.
/// Returns 1 on success, 0 if SetElapsedTime is unavailable.
int touchdeck_now_playing_set_elapsed(double elapsed_sec) {
  if (!g_setElapsed) {
    // Ensure symbol is resolved (same once-block as poll).
    touchdeck_now_playing_poll_async();
  }
  if (!g_setElapsed) {
    return 0;
  }
  if (elapsed_sec < 0) elapsed_sec = 0;
  g_setElapsed(elapsed_sec);
  return 1;
}

/// Best-effort playback speed. `speed_x100` is rate×100 (100=1×, 150=1.5×).
/// Many apps (Safari/YouTube) ignore this; browser JS / hotkeys are used from Rust.
int touchdeck_now_playing_set_speed_x100(int speed_x100) {
  if (!g_setSpeed) {
    touchdeck_now_playing_poll_async();
  }
  if (!g_setSpeed) {
    return 0;
  }
  if (speed_x100 < 25) speed_x100 = 25;
  if (speed_x100 > 400) speed_x100 = 400;
  // Some clients expect percent (100=1x); others expect small ints — try percent first.
  g_setSpeed(speed_x100);
  return 1;
}

/// Synchronous MediaRemote snapshot into the cached JSON.
/// Note: on macOS 15.4+ GetNowPlayingInfo is often empty for third-party apps;
/// the Rust layer prefers JXA MRNowPlayingRequest for reads.
void touchdeck_now_playing_poll_async(void) {
  static void *handle = NULL;
  static MRMediaRemoteGetNowPlayingInfoFn getInfo = NULL;
  static MRMediaRemoteGetNowPlayingApplicationIsPlayingFn getPlaying = NULL;
  static dispatch_once_t once;
  dispatch_once(&once, ^{
    handle = dlopen("/System/Library/PrivateFrameworks/MediaRemote.framework/MediaRemote", RTLD_NOW);
    if (!handle) return;
    getInfo = (MRMediaRemoteGetNowPlayingInfoFn)dlsym(handle, "MRMediaRemoteGetNowPlayingInfo");
    getPlaying = (MRMediaRemoteGetNowPlayingApplicationIsPlayingFn)dlsym(
        handle, "MRMediaRemoteGetNowPlayingApplicationIsPlaying");
    g_setElapsed = (MRMediaRemoteSetElapsedTimeFn)dlsym(handle, "MRMediaRemoteSetElapsedTime");
    g_setSpeed = (MRMediaRemoteSetPlaybackSpeedFn)dlsym(handle, "MRMediaRemoteSetPlaybackSpeed");
  });

  if (!getInfo) {
    pthread_mutex_lock(&g_mu);
    set_json_locked(NULL);
    pthread_mutex_unlock(&g_mu);
    return;
  }

  dispatch_queue_t q = dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0);
  dispatch_semaphore_t sem = dispatch_semaphore_create(0);
  __block Boolean isPlaying = false;
  __block NSDictionary *infoCopy = nil;

  if (getPlaying) {
    getPlaying(q, ^(Boolean playing) {
      isPlaying = playing;
      getInfo(q, ^(NSDictionary *info) {
        infoCopy = [info copy];
        dispatch_semaphore_signal(sem);
      });
    });
  } else {
    getInfo(q, ^(NSDictionary *info) {
      infoCopy = [info copy];
      dispatch_semaphore_signal(sem);
    });
  }

  // Wait up to 400ms for the callback.
  dispatch_semaphore_wait(sem, dispatch_time(DISPATCH_TIME_NOW, 400 * NSEC_PER_MSEC));

  NSDictionary *info = infoCopy;
  if (!info || info.count == 0) {
    pthread_mutex_lock(&g_mu);
    set_json_locked(NULL);
    pthread_mutex_unlock(&g_mu);
    return;
  }

  NSString *title = dict_str(info, @[
    @"kMRMediaRemoteNowPlayingInfoTitle", @"Title", @"title"
  ]);
  NSString *artist = dict_str(info, @[
    @"kMRMediaRemoteNowPlayingInfoArtist", @"Artist", @"artist"
  ]);
  NSString *album = dict_str(info, @[
    @"kMRMediaRemoteNowPlayingInfoAlbum", @"Album", @"album"
  ]);
  NSNumber *dur = dict_num(info, @[
    @"kMRMediaRemoteNowPlayingInfoDuration", @"Duration", @"duration"
  ]);
  NSNumber *elapsed = dict_num(info, @[
    @"kMRMediaRemoteNowPlayingInfoElapsedTime", @"ElapsedTime", @"elapsedTime"
  ]);
  NSNumber *rate = dict_num(info, @[
    @"kMRMediaRemoteNowPlayingInfoPlaybackRate", @"PlaybackRate", @"playbackRate"
  ]);

  if (rate && [rate doubleValue] > 0.01) {
    isPlaying = true;
  }

  double dur_s = dur ? [dur doubleValue] : 0;
  double pos_s = elapsed ? [elapsed doubleValue] : 0;
  uint32_t dur_ms = dur_s > 0 ? (uint32_t)(dur_s * 1000.0) : 0;
  uint32_t pos_ms = pos_s > 0 ? (uint32_t)(pos_s * 1000.0) : 0;

  NSString *app = album;  // best-effort until client name is available

  NSString *json = [NSString
      stringWithFormat:
          @"{\"title\":\"%@\",\"artist\":\"%@\",\"playing\":%@,\"pos_ms\":%u,\"dur_ms\":%u,\"app\":\"%@\"}",
          escape_json(title), escape_json(artist), isPlaying ? @"true" : @"false", pos_ms, dur_ms,
          escape_json(app)];

  pthread_mutex_lock(&g_mu);
  set_json_locked([json UTF8String]);
  pthread_mutex_unlock(&g_mu);
}
