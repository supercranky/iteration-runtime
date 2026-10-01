// Local-development transport. Network/file work stays off the frame thread;
// only main-queue callbacks expose a completed immutable generation to the VM.
#import <Foundation/Foundation.h>
#import <CommonCrypto/CommonDigest.h>
#include "hot_reload.h"
#include "util/fileutil.h"
#include <stdlib.h>
#include <string.h>

static NSString *endpoint, *lastRevision, *activeRoot, *stagedRoot, *cacheRoot, *bundleIdentity;
static NSString *connectionStatus = @"Disabled";
static char *savedState;
static BOOL started, busy;
static dispatch_queue_t worker;
static const NSUInteger maxFile = 32 * 1024 * 1024, maxTotal = 256 * 1024 * 1024;

// Enforce this in the client, not just the server manifest. Release plugins
// are part of the executable; no downloaded or cached WASM may shadow them.
static BOOL bundledWasm(NSString *name) {
#if defined(ITERATION_HOT_RELOAD_NO_WASM) || defined(ITERATION_NATIVE_PLUGINS)
  return [name.pathExtension caseInsensitiveCompare:@"wasm"]==NSOrderedSame;
#else
  (void)name;return NO;
#endif
}
static NSString *digest(NSData *data) {
  unsigned char bytes[CC_SHA256_DIGEST_LENGTH];CC_SHA256(data.bytes,(CC_LONG)data.length,bytes);
  NSMutableString *s=[NSMutableString string];for(int i=0;i<CC_SHA256_DIGEST_LENGTH;i++)[s appendFormat:@"%02x",bytes[i]];return s;
}
static BOOL safePath(NSString *name) {
  if(![name isKindOfClass:NSString.class]||!name.length||name.length>=240)return NO;
  NSCharacterSet *bad=[[NSCharacterSet characterSetWithCharactersInString:@"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_ .-/"] invertedSet];
  if([name rangeOfCharacterFromSet:bad].location!=NSNotFound)return NO;
  for(NSString *part in [name componentsSeparatedByString:@"/"])if(!part.length||[part isEqual:@"."]||[part isEqual:@".."])return NO;
  return YES;
}
static NSData *get(NSString *url, NSUInteger limit) {
  NSURL *target=[NSURL URLWithString:url];if(!target)return nil;
  NSMutableURLRequest *request=[NSMutableURLRequest requestWithURL:target cachePolicy:NSURLRequestReloadIgnoringLocalCacheData timeoutInterval:8];
  // A file download avoids allocating unbounded response bodies in the runtime.
  dispatch_semaphore_t done=dispatch_semaphore_create(0);
  __block NSData *result=nil;
  NSURLSessionDownloadTask *task=[NSURLSession.sharedSession downloadTaskWithRequest:request completionHandler:^(NSURL *file,NSURLResponse *response,NSError *error){
    if(!error&&[(NSHTTPURLResponse *)response statusCode]==200) {
      NSNumber *size=nil;[file getResourceValue:&size forKey:NSURLFileSizeKey error:nil];
      if(size&&size.unsignedLongLongValue<=limit)result=[NSData dataWithContentsOfURL:file];
    }
    dispatch_semaphore_signal(done);
  }];
  [task resume];dispatch_semaphore_wait(done,DISPATCH_TIME_FOREVER);return result;
}
static NSString *bundlePath(NSString *name) {
  char path[512];return [NSString stringWithUTF8String:fileutil_get_path(name.UTF8String,path,sizeof(path))];
}
static NSString *stage(NSDictionary *manifest,NSString *oldRoot) {
  NSArray *files=manifest[@"files"];NSString *revision=manifest[@"id"];
  if(![files isKindOfClass:NSArray.class]||files.count>2048||!safePath(revision)||[revision containsString:@"/"])return nil;
  NSString *root=[cacheRoot stringByAppendingPathComponent:NSUUID.UUID.UUIDString];
  NSFileManager *fm=NSFileManager.defaultManager;
  if(![fm createDirectoryAtPath:root withIntermediateDirectories:YES attributes:nil error:nil])return nil;
  NSMutableSet *seen=[NSMutableSet set];NSUInteger total=0;BOOL ok=YES;
  for(NSDictionary *entry in files) {
    if(![entry isKindOfClass:NSDictionary.class]){ok=NO;break;}
    NSString *name=entry[@"path"],*sha=entry[@"hash"];NSNumber *size=entry[@"size"];
    if(!safePath(name)||[seen containsObject:name]){ok=NO;break;}
    [seen addObject:name];
    if(bundledWasm(name))continue; // Includes additions, changes and removals.
    if(![sha isKindOfClass:NSString.class]||sha.length!=64||
       ![size isKindOfClass:NSNumber.class]||size.longLongValue<0||size.unsignedLongLongValue>maxFile){ok=NO;break;}
    total+=size.unsignedIntegerValue;if(total>maxTotal){ok=NO;break;}
    NSString *source=oldRoot?[oldRoot stringByAppendingPathComponent:name]:bundlePath(name);
    NSData *data=[NSData dataWithContentsOfFile:source options:NSDataReadingMappedIfSafe error:nil];
    if(!data||data.length!=size.unsignedIntegerValue||![digest(data) isEqual:sha]) {
      NSString *escaped=[name stringByAddingPercentEncodingWithAllowedCharacters:NSCharacterSet.URLPathAllowedCharacterSet];
      data=get([NSString stringWithFormat:@"%@/__hot/assets/%@/%@",endpoint,revision,escaped],maxFile);
    }
    if(!data||data.length!=size.unsignedIntegerValue||![digest(data) isEqual:sha]){ok=NO;break;}
    NSString *destination=[root stringByAppendingPathComponent:name];
    if(![fm createDirectoryAtPath:destination.stringByDeletingLastPathComponent withIntermediateDirectories:YES attributes:nil error:nil]||
       ![data writeToFile:destination options:NSDataWritingAtomic error:nil]){ok=NO;break;}
  }
  if(!ok||![seen containsObject:@"index.js"]){[fm removeItemAtPath:root error:nil];return nil;}
  return root;
}
static void poll(void) {
  if(!endpoint)return;
  if(!busy&&!stagedRoot) {
    busy=YES;NSString *old=activeRoot,*last=lastRevision;
    dispatch_async(worker,^{@autoreleasepool {
      NSData *data=get([endpoint stringByAppendingString:@"/__hot/manifest"],1024*1024);
      NSDictionary *manifest=data?[NSJSONSerialization JSONObjectWithData:data options:0 error:nil]:nil;
      BOOL valid=[manifest isKindOfClass:NSDictionary.class]&&[manifest[@"protocol"] isEqual:@1]&&safePath(manifest[@"id"]);
      NSString *revision=valid?manifest[@"id"]:nil;
      NSString *root=nil;
      if(valid&&last&&![last isEqual:revision]) {
        dispatch_async(dispatch_get_main_queue(),^{connectionStatus=@"Downloading assets";});
        root=stage(manifest,old);
      }
      dispatch_async(dispatch_get_main_queue(),^{
        busy=NO;
        if(!valid)connectionStatus=@"Disconnected — retrying";
        else if(!last||[last isEqual:revision]){lastRevision=revision;connectionStatus=@"Connected";}
        else if(root){stagedRoot=root;lastRevision=revision;connectionStatus=@"Waiting for safe state";}
        else connectionStatus=@"Asset transfer failed — retrying";
      });
    }});
  }
  dispatch_after(dispatch_time(DISPATCH_TIME_NOW,NSEC_PER_SEC),dispatch_get_main_queue(),^{poll();});
}
void hot_reload_start(void) {
  if(started)return;started=YES;
  NSData *data=[NSData dataWithContentsOfFile:bundlePath(@"hot-reload.json")];
  NSDictionary *config=data?[NSJSONSerialization JSONObjectWithData:data options:0 error:nil]:nil;
  NSString *url=[config isKindOfClass:NSDictionary.class]?config[@"url"]:nil;
  if(![url isKindOfClass:NSString.class])return;
  NSURL *parsed=[NSURL URLWithString:url];
  if(!([parsed.scheme isEqual:@"http"]||[parsed.scheme isEqual:@"https"])||!parsed.host)return;
  endpoint=[url hasSuffix:@"/"]?[url substringToIndex:url.length-1]:url;
  worker=dispatch_queue_create("iteration.hot-reload",DISPATCH_QUEUE_SERIAL);
  // Writable generations replace bundle assets without trying to mutate the
  // signed .app. Preserve the active mirror on relaunch, but never let a cache
  // from an older development install override a newly built application.
  NSString *support=[NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory,NSUserDomainMask,YES) firstObject];
#if defined(ITERATION_HOT_RELOAD_HOST_TEST)
  support=[NSString stringWithUTF8String:getenv("TEST_CACHE_ROOT")];
#endif
  cacheRoot=[support stringByAppendingPathComponent:@"iteration-hot-reload"];
  NSFileManager *fm=NSFileManager.defaultManager;
  [fm createDirectoryAtPath:cacheRoot withIntermediateDirectories:YES attributes:nil error:nil];
  NSMutableData *identity=[data mutableCopy];
  NSData *entry=[NSData dataWithContentsOfFile:bundlePath(@"index.js")];if(entry)[identity appendData:entry];
  NSString *policy=bundledWasm(@"plugin.wasm")?@"bundled-wasm-v1":@"dynamic-wasm-v1";
  [identity appendData:[policy dataUsingEncoding:NSUTF8StringEncoding]];
  bundleIdentity=digest(identity);
  NSData *pointer=[NSData dataWithContentsOfFile:[cacheRoot stringByAppendingPathComponent:@"active.json"]];
  NSDictionary *active=pointer?[NSJSONSerialization JSONObjectWithData:pointer options:0 error:nil]:nil;
  if([active isKindOfClass:NSDictionary.class]&&[bundleIdentity isEqual:active[@"bundle"]]&&safePath(active[@"root"])&&
     ![active[@"root"] containsString:@"/"]&&safePath(active[@"revision"])) {
    NSString *root=[cacheRoot stringByAppendingPathComponent:active[@"root"]];
    if([fm fileExistsAtPath:[root stringByAppendingPathComponent:@"index.js"]]){activeRoot=root;lastRevision=active[@"revision"];}
  }
  for(NSString *name in [fm contentsOfDirectoryAtPath:cacheRoot error:nil]) {
    NSString *file=[cacheRoot stringByAppendingPathComponent:name];
    if(![file isEqual:activeRoot]&&![name isEqual:@"active.json"])[fm removeItemAtPath:file error:nil];
  }
  connectionStatus=@"Connecting";poll();
}
int hot_reload_pending(void) {return stagedRoot!=nil;}
int hot_reload_commit(const char *state) {
  if(!stagedRoot)return 0;
  char *copy=strdup(state);if(!copy)return 0;
  NSData *pointer=[NSJSONSerialization dataWithJSONObject:@{@"root":stagedRoot.lastPathComponent,@"revision":lastRevision,@"bundle":bundleIdentity} options:0 error:nil];
  if(![pointer writeToFile:[cacheRoot stringByAppendingPathComponent:@"active.json"] options:NSDataWritingAtomic error:nil]) {
    free(copy);connectionStatus=@"Cannot commit asset generation";return 0;
  }
  free(savedState);savedState=copy;
  NSString *old=activeRoot;activeRoot=stagedRoot;stagedRoot=nil;connectionStatus=@"Connected";
  if(old)dispatch_async(worker,^{[NSFileManager.defaultManager removeItemAtPath:old error:nil];});
  return 1;
}
const char *hot_reload_status(void) {
  if(bundledWasm(@"plugin.wasm")) {
    static char status[192];
    snprintf(status,sizeof(status),"%s (WASM locked)",connectionStatus.UTF8String);return status;
  }
  return connectionStatus.UTF8String;
}
char *hot_reload_take_state(void) {char *state=savedState;savedState=NULL;return state;}
const char *hot_reload_asset_path(const char *name,char *buffer,size_t size) {
  NSString *relative=[NSString stringWithUTF8String:name];
  if(bundledWasm(relative)) {
    if(!safePath(relative))return "__invalid_hot_reload_path__";
    return fileutil_get_path(name,buffer,size);
  }
  if(activeRoot) {
    // Complete mirrors make non-WASM deletions real; Release WASM stays bundled.
    if(!safePath(relative))return "__invalid_hot_reload_path__";
    int n=snprintf(buffer,size,"%s/%s",activeRoot.UTF8String,name);
    return n>=0&&(size_t)n<size?buffer:"__hot_reload_path_too_long__";
  }
  return fileutil_get_path(name,buffer,size);
}
