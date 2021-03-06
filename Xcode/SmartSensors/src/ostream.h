/*
 * OStream supports emitting prediction results to some downstream consumers,
 * e.g. Mac OS Keyboard Emulator, OSC Stream, TCP Stream, etc.
 *
 * OStream* ostream = new MacOSKeyboardOStream(3, '\0', 'f', 'd');
 * OStream* ostream = new MacOSMouseOStream(3, 0, 0, 240, 240, 400, 400);
 * OStream* ostream = new TcpOStream("localhost", 9999, 3, "", "mouse 300, 300.", "mouse 400, 400.");
 * OStream* ostream = new TcpOStream("localhost", 5204, 3, "l", "r", " ");
 *
 */
#pragma once

#include <ApplicationServices/ApplicationServices.h>

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ofMain.h"
#include "ofApp.h"
#include "ofxTCPClient.h"

const uint64_t kGracePeriod = 500; // 0.5 second

// Forward declaration.
class ofApp;

class OStream {
  public:
    virtual void onReceive(uint32_t label) = 0;

    virtual bool start() { has_started_ = true; return true; }
    bool hasStarted() { return has_started_; }
  protected:
    bool has_started_ = false;
};

// MacOSKeyboardOStream will emulate keyboard key press event upon receiving
// classification results (via the callback `onReceive`). Users of this class
// can supply a map<integer, char> so that the labels will be translated to
// proper key strokes.
class MacOSKeyboardOStream : public OStream {
  public:
    MacOSKeyboardOStream(std::map<uint32_t, char> key_mapping)
            : key_mapping_(key_mapping) {
    }

    MacOSKeyboardOStream(uint32_t count, ...) {
        va_list args;
        va_start(args, count);
        for (uint32_t i = 1; i <= count; i++) {
            key_mapping_[i] = va_arg(args, int);
        }
        va_end(args);
    }

    virtual void onReceive(uint32_t label) {
        if (has_started_) {
            if (getChar(label) != '\0') {
                sendKey(getChar(label));
            }
        }
    }

  private:
    void sendKey(char c) {
        if (ofGetElapsedTimeMillis() < elapsed_time_ + kGracePeriod) {
            return;
        }
        elapsed_time_ = ofGetElapsedTimeMillis();

        // Get the process number for the front application.
        ProcessSerialNumber psn = { 0, kNoProcess };
        GetFrontProcess( &psn );

        UniChar uni_char = c;
        CGEventRef key_down = CGEventCreateKeyboardEvent(NULL, 0, true);
        CGEventRef key_up = CGEventCreateKeyboardEvent(NULL, 0, false);
        CGEventKeyboardSetUnicodeString(key_down, 1, &uni_char);
        CGEventKeyboardSetUnicodeString(key_up, 1, &uni_char);
        CGEventPostToPSN(&psn, key_down);
        CGEventPostToPSN(&psn, key_up);
        CFRelease(key_down);
        CFRelease(key_up);
    }

    void sendString(const std::string& str) {
        // Get the process number for the front application.
        ProcessSerialNumber psn = { 0, kNoProcess };
        GetFrontProcess( &psn );

        UniChar s[str.length()];
        for (uint32_t i = 0; i < str.length(); i++) {
            s[i] = str[i];
        }

        CGEventRef e = CGEventCreateKeyboardEvent(NULL, 0, true);
        CGEventKeyboardSetUnicodeString(e, str.length(), s);
        CGEventPostToPSN(&psn, e);
        CFRelease(e);
    }

    char getChar(uint32_t label) {
        return key_mapping_[label];
    }

    uint64_t elapsed_time_ = 0;
    std::map<uint32_t, char> key_mapping_;
};

// MacOSMouseOStream
class MacOSMouseOStream : public OStream {
  public:
    MacOSMouseOStream(std::map<uint32_t, pair<uint32_t, uint32_t> > mouse_mapping)
    : mouse_mapping_(mouse_mapping) {
    }

    MacOSMouseOStream(uint32_t count, ...) {
        va_list args;
        va_start(args, count);
        for (uint32_t i = 1; i <= count; i++) {
            mouse_mapping_[i] = make_pair(va_arg(args, uint32_t),
                                          va_arg(args, uint32_t));

        }
        va_end(args);
    }

    virtual void onReceive(uint32_t label) {
        if (has_started_) {
            pair<uint32_t, uint32_t> mouse = getMousePosition(label);
            if (mouse.first > 0 & mouse.second > 0) {
                clickMouse(mouse);
            }
        }
    }

private:
    void clickMouse(pair<uint32_t, uint32_t> mouse) {
        if (ofGetElapsedTimeMillis() < elapsed_time_ + kGracePeriod) {
            return;
        }
        elapsed_time_ = ofGetElapsedTimeMillis();

        doubleClick(CGPointMake(mouse.first, mouse.second));
    }

    void doubleClick(CGPoint point, int clickCount = 2) {
        CGEventRef theEvent = CGEventCreateMouseEvent(
            NULL, kCGEventLeftMouseDown, point, kCGMouseButtonLeft);

        ProcessSerialNumber psn = { 0, kNoProcess };
        GetFrontProcess( &psn );

        CGEventSetIntegerValueField(theEvent, kCGMouseEventClickState, clickCount);
        CGEventPostToPSN(&psn, theEvent);
        CGEventSetType(theEvent, kCGEventLeftMouseUp);
        CGEventPostToPSN(&psn, theEvent);
        CGEventSetType(theEvent, kCGEventLeftMouseDown);
        CGEventPostToPSN(&psn, theEvent);
        CGEventSetType(theEvent, kCGEventLeftMouseUp);
        CGEventPostToPSN(&psn, theEvent);
        CFRelease(theEvent);
    }

    pair<uint32_t, uint32_t> getMousePosition(uint32_t label) {
        return mouse_mapping_[label];
    }

    uint64_t elapsed_time_ = 0;
    std::map<uint32_t, pair<uint32_t, uint32_t>> mouse_mapping_;
};

// TcpOStream
class TcpOStream : public OStream {
  public:
    TcpOStream(string server, int port,
               std::map<uint32_t, string> tcp_stream_mapping)
            : server_(server), port_(port),
            tcp_stream_mapping_(tcp_stream_mapping) {
    }

    TcpOStream(string server, int port, uint32_t count, ...)
            : server_(server), port_(port) {
        va_list args;
        va_start(args, count);
        for (uint32_t i = 1; i <= count; i++) {
            char* s = va_arg(args, char *);
            tcp_stream_mapping_[i] = std::string(s);
        }
        va_end(args);
    }

    virtual void onReceive(uint32_t label) {
        if (has_started_) {
            string to_send = getStreamString(label);
            if (!to_send.empty()) {
                sendString(to_send);
            }
        }
    }

    bool start() {
        has_started_ = client_.setup(server_, port_);
        client_.setMessageDelimiter("\n");
        return has_started_;
    }

private:
    void sendString(const string& tosend) {
        if (ofGetElapsedTimeMillis() < elapsed_time_ + kGracePeriod) {
            return;
        }
        elapsed_time_ = ofGetElapsedTimeMillis();

        if (client_.isConnected()) {
            client_.send(tosend);
        }
    }

    string getStreamString(uint32_t label) {
        return tcp_stream_mapping_[label];
    }

    string server_;
    int port_;
    ofxTCPClient client_;

    uint64_t elapsed_time_ = 0;
    std::map<uint32_t, string> tcp_stream_mapping_;
};

void useOStream(OStream &stream);
