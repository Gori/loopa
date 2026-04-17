#import <Foundation/Foundation.h>

#include "LogPath.h"

namespace loopa::platform {

std::string userLogDir() {
    @autoreleasepool {
        NSArray<NSURL*>* urls = [[NSFileManager defaultManager]
            URLsForDirectory:NSLibraryDirectory inDomains:NSUserDomainMask];
        if (urls.count == 0) {
            return {};
        }
        NSURL* library = urls.firstObject;
        NSURL* logs = [library URLByAppendingPathComponent:@"Logs" isDirectory:YES];
        NSURL* loopa = [logs URLByAppendingPathComponent:@"Loopa" isDirectory:YES];
        return std::string(loopa.path.UTF8String);
    }
}

}  // namespace loopa::platform
