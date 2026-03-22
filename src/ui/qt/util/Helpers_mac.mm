#import <CoreFoundation/CoreFoundation.h>
#import <AppKit/AppKit.h>
#import <QuartzCore/QuartzCore.h>
#include <QUrl>
#include <QWidget>
#include <QWindow>
#include <qpa/qplatformwindow_p.h>

#include "LogManager.h"

void qtOpenUrlNative(const QByteArray& encodedUrl) {
  @autoreleasepool {
    CFURLRef cfUrl = CFURLCreateWithBytes(
        kCFAllocatorDefault,
        reinterpret_cast<const UInt8*>(encodedUrl.constData()),
        encodedUrl.size(),
        kCFStringEncodingUTF8,
        nullptr  // no base URL
    );

    if (cfUrl) {
      NSURL* nsUrl = (__bridge_transfer NSURL*)cfUrl;
      [[NSWorkspace sharedWorkspace] openURL:nsUrl];
    } else {
      L_ERROR("Could not open bug URL: {}", encodedUrl.constData());
    }
  }
}

void qtUpdateFramelessWindowCornersNative(QWidget* widget, double cornerRadius) {
  @autoreleasepool {
    if (!widget) {
      return;
    }

    QWindow* qtWindow = widget->window()->windowHandle();
    if (!qtWindow) {
      return;
    }

    auto *cocoaWindow = qtWindow->nativeInterface<QNativeInterface::Private::QCocoaWindow>();
    if (!cocoaWindow) {
      return;
    }

    CALayer* layer = cocoaWindow->contentLayer();
    if (!layer) {
      return;
    }

    layer.masksToBounds = cornerRadius > 0.0;
    layer.cornerRadius = cornerRadius;
    if (@available(macOS 10.15, *)) {
      layer.cornerCurve = @"continuous";
    }
  }
}
