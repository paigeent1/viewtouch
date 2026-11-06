#pragma once

// The guest screen's slides (Store Settings > Customer display: between guests, one per line):
//   a message                      shown 7 seconds
//   image:<ref>                    a picture, 10 seconds (store:guest-<name>.jpg, or a path)
//   video:<absolute path>          a video, played once to its end, muted
//   video:<absolute path>#sound    the same with its sound (the guest monitor's speakers)
// The "#sound" is only ever at the end (a video's own name never has a '#': addGuestVideo takes
// it out). The guest screen reads them in CustomerDisplay.qml and WidgetGuestMessages.qml.

#include <QString>

namespace vt::guest {

inline constexpr QStringView kVideoPrefix = u"video:";
inline constexpr QStringView kSoundMark = u"#sound";

struct VideoSlide {
    QString path;        // empty: not a video slide
    bool sound = false;
};

inline bool isVideoSlide(const QString &line) { return line.startsWith(kVideoPrefix); }

inline VideoSlide parseVideoSlide(const QString &line)
{
    if (!isVideoSlide(line))
        return {};
    QString path = line.mid(kVideoPrefix.size());
    const bool sound = path.endsWith(kSoundMark);
    if (sound)
        path.chop(kSoundMark.size());
    return {path, sound};
}

inline QString videoSlide(const QString &path, bool sound)
{
    return kVideoPrefix.toString() + path + (sound ? kSoundMark.toString() : QString());
}

// The same video with its sound on or off; any other line as it is.
inline QString withSound(const QString &line, bool sound)
{
    const VideoSlide v = parseVideoSlide(line);
    return v.path.isEmpty() ? line : videoSlide(v.path, sound);
}

} // namespace vt::guest
