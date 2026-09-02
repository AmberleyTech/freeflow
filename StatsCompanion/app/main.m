/* FreeFlow Stats viewer — a minimal WKWebView wrapper around the sidecar's
 * local stats page, so the stats open as a proper macOS app: Dock icon,
 * Spotlight, Cmd-Tab, assignable keyboard shortcuts.
 *
 * On-demand by design: it refreshes stats from the sidecar binary, shows the
 * page, and quits completely when the window closes. The always-on collector
 * remains the 0 MB-idle launchd agent; this viewer is only resident while
 * you are looking at it (WebKit uses tens of MB while open — see README).
 */
#import <Cocoa/Cocoa.h>
#import <WebKit/WebKit.h>

@interface FFSAppDelegate : NSObject <NSApplicationDelegate>
@property(nonatomic, strong) NSWindow *window;
@end

@implementation FFSAppDelegate

- (void)applicationDidFinishLaunching:(NSNotification *)notification {
    NSString *supportDir =
        [NSHomeDirectory() stringByAppendingPathComponent:
                               @"Library/Application Support/FreeFlowStats"];
    NSString *htmlPath =
        [supportDir stringByAppendingPathComponent:@"stats.html"];
    NSString *binPath =
        [supportDir stringByAppendingPathComponent:@"bin/freeflow-stats"];

    /* Best-effort refresh: the binary ingests in milliseconds and exits.
     * The launchd agent also keeps stats fresh between dictations. */
    if ([[NSFileManager defaultManager] isExecutableFileAtPath:binPath]) {
        NSTask *task = [[NSTask alloc] init];
        task.launchPath = binPath;
        [task launch];
        [task waitUntilExit];
    }

    NSRect frame = NSMakeRect(0, 0, 780, 920);
    self.window = [[NSWindow alloc]
        initWithContentRect:frame
                  styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                             NSWindowStyleMaskMiniaturizable |
                             NSWindowStyleMaskResizable)
                    backing:NSBackingStoreBuffered
                      defer:NO];
    self.window.title = @"FreeFlow Stats";
    [self.window center];

    WKWebView *webView = [[WKWebView alloc] initWithFrame:frame];
    webView.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    self.window.contentView = webView;

    if ([[NSFileManager defaultManager] fileExistsAtPath:htmlPath]) {
        NSURL *pageURL = [NSURL fileURLWithPath:htmlPath];
        [webView loadFileURL:pageURL
            allowingReadAccessToURL:[NSURL fileURLWithPath:supportDir]];
    } else {
        [webView loadHTMLString:
                     @"<html><body style='font-family:-apple-system;"
                      "padding:48px;line-height:1.6'>"
                      "<h2>FreeFlow Stats is not set up yet</h2>"
                      "<p>From your FreeFlow fork, run:</p>"
                      "<p><code>cd StatsCompanion && make && make install</code></p>"
                      "<p>Then dictate once in FreeFlow and reopen this app.</p>"
                      "</body></html>"
                 baseURL:nil];
    }

    [self.window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:
    (NSApplication *)sender {
    return YES;
}

@end

int main(int argc, const char *argv[]) {
    @autoreleasepool {
        NSApplication *app = [NSApplication sharedApplication];
        FFSAppDelegate *delegate = [[FFSAppDelegate alloc] init];
        app.delegate = delegate;
        [app setActivationPolicy:NSApplicationActivationPolicyRegular];
        [app run];
    }
    return 0;
}
