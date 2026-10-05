// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <cstdint>
#include <string>
#include <vector>

namespace opennow::audio
{
enum class Mode
{
    automatic,
    stereo,
    surround51,
    surround71
};
inline Mode nextMode(Mode mode)
{
    return static_cast<Mode>((static_cast<unsigned>(mode) + 1) % 4);
}
inline const char *modeLabel(Mode mode)
{
    switch (mode)
    {
    case Mode::stereo:
        return "STEREO";
    case Mode::surround51:
        return "5.1";
    case Mode::surround71:
        return "7.1";
    default:
        return "AUTO";
    }
}
constexpr unsigned requestedChannels(Mode mode, unsigned capacity)
{
    const unsigned limit = mode == Mode::stereo ? 2 : mode == Mode::surround51 ? 6 : 8;
    return capacity >= 8 && limit >= 8 ? 8 : capacity >= 6 && limit >= 6 ? 6 : 2;
}
struct Format
{
    unsigned channels = 0, streams = 0, coupled = 0;
    int payload = -1, redPayload = -1;
    std::array<unsigned char, 8> mapping{};
    std::string rtpmap, fmtp, redRtpmap, redFmtp, mid;
    unsigned mediaIndex = 0;
    unsigned bitrate() const
    {
        return channels == 8 ? 510000 : channels == 6 ? 384000 : 256000;
    }
    bool operator==(const Format &) const = default;
};
inline bool validFormat(const Format &format) noexcept
{
    if ((format.channels != 2 && format.channels != 6 && format.channels != 8) || !format.streams ||
        format.streams > format.channels || format.coupled > format.streams ||
        format.streams + format.coupled != format.channels || format.payload < 0 ||
        format.payload > 127 || format.redPayload < -1 || format.redPayload > 127 ||
        format.redPayload == format.payload)
        return false;
    if (format.channels == 2 && (format.streams != 1 || format.coupled != 1 ||
                                 format.mapping[0] != 0 || format.mapping[1] != 1))
        return false;
    unsigned seen = 0;
    for (unsigned i = 0; i < format.channels; ++i)
    {
        if (format.mapping[i] >= format.channels)
            return false;
        seen |= 1u << format.mapping[i];
    }
    return seen == (1u << format.channels) - 1;
}
inline std::string trim(const std::string &value)
{
    const auto first = value.find_first_not_of(" \t\r\n");
    return first == std::string::npos
               ? std::string{}
               : value.substr(first, value.find_last_not_of(" \t\r\n") - first + 1);
}
inline bool integer(const std::string &value, unsigned &result)
{
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    return !value.empty() && parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size();
}
inline std::vector<std::string> split(const std::string &value, char separator)
{
    std::vector<std::string> result;
    std::size_t start = 0;
    for (;;)
    {
        const auto end = value.find(separator, start);
        result.push_back(value.substr(start, end == std::string::npos ? end : end - start));
        if (end == std::string::npos)
            break;
        start = end + 1;
    }
    return result;
}
inline std::string lower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}
inline bool parameter(const std::string &fmtp, const char *key, std::string &value)
{
    bool found = false;
    for (const auto &item : split(fmtp, ';'))
    {
        const auto equal = item.find('=');
        if (equal == std::string::npos)
            continue;
        if (lower(trim(item.substr(0, equal))) != key)
            continue;
        if (found)
            return false;
        found = true;
        value = trim(item.substr(equal + 1));
    }
    return found && !value.empty();
}
inline Format selectFormat(const std::string &offer, unsigned maximum)
{
    if (offer.empty() || offer.size() > 65536 || maximum < 2)
        return {};
    std::array<std::string, 128> maps{}, params{};
    std::array<bool, 128> offered{}, videoPayloads{};
    bool inAudio = false, activeAudio = false, sessionCanSend = true, canSend = true;
    unsigned audioSections = 0, mediaIndex = 0, audioIndex = 0, lineCount = 0;
    std::string mid;
    for (auto line : split(offer, '\n'))
    {
        if (++lineCount > 2048 || line.size() > 2048)
            return {};
        line = trim(line);
        if (line.rfind("m=", 0) == 0)
        {
            std::replace(line.begin(), line.end(), '\t', ' ');
            std::vector<std::string> fields;
            for (const auto &field : split(line, ' '))
                if (!field.empty())
                    fields.push_back(field);
            inAudio = !fields.empty() && fields[0] == "m=audio";
            unsigned port = 0;
            if (fields.size() < 4 || !integer(fields[1], port) || port > 65535)
                return {};
            if (inAudio)
            {
                if (++audioSections > 1)
                    return {};
                activeAudio = port != 0;
                canSend = sessionCanSend;
                audioIndex = mediaIndex;
            }
            if (inAudio || (fields[0] == "m=video" && port))
            {
                for (std::size_t i = 3; i < fields.size(); ++i)
                {
                    unsigned pt = 0;
                    if (!integer(fields[i], pt) || pt > 127)
                        return {};
                    (inAudio ? offered : videoPayloads)[pt] = true;
                }
            }
            ++mediaIndex;
            continue;
        }
        if (!mediaIndex && (line == "a=recvonly" || line == "a=inactive"))
            sessionCanSend = false;
        if (!inAudio)
            continue;
        if (line == "a=recvonly" || line == "a=inactive")
            canSend = false;
        if (line.rfind("a=mid:", 0) == 0)
        {
            if (!mid.empty())
                return {};
            mid = line.substr(6);
        }
        const bool map = line.rfind("a=rtpmap:", 0) == 0;
        if (!map && line.rfind("a=fmtp:", 0) != 0)
            continue;
        const auto start = map ? 9u : 7u;
        const auto space = line.find_first_of(" \t", start);
        unsigned pt = 0;
        if (space == std::string::npos || !integer(line.substr(start, space - start), pt) ||
            pt > 127)
            return {};
        const auto value = trim(line.substr(space + 1));
        auto &stored = (map ? maps : params)[pt];
        if (!stored.empty() && stored != value)
            return {};
        stored = value;
    }
    if (!activeAudio || !canSend)
        return {};
    Format best;
    for (unsigned pt = 0; pt < 128; ++pt)
    {
        if (!offered[pt] || videoPayloads[pt])
            continue;
        const auto map = lower(maps[pt]);
        Format candidate;
        candidate.payload = static_cast<int>(pt);
        candidate.mediaIndex = audioIndex;
        candidate.mid = mid;
        if (map == "opus/48000/2")
        {
            candidate.channels = 2;
            candidate.streams = candidate.coupled = 1;
            candidate.mapping = {0, 1};
            candidate.fmtp =
                "minptime=10;stereo=1;sprop-stereo=1;maxaveragebitrate=256000;useinbandfec=1";
        }
        else if (map == "multiopus/48000/6" || map == "multiopus/48000/8")
        {
            candidate.channels = map.back() == '6' ? 6 : 8;
            std::string streams, coupled, mapping;
            if (!parameter(params[pt], "num_streams", streams) ||
                !parameter(params[pt], "coupled_streams", coupled) ||
                !parameter(params[pt], "channel_mapping", mapping) ||
                !integer(streams, candidate.streams) || !integer(coupled, candidate.coupled))
                continue;
            unsigned count = 0, index = 0;
            bool valid = true;
            std::string canonical;
            for (const auto &field : split(mapping, ','))
            {
                if (count >= candidate.channels || !integer(trim(field), index) ||
                    index >= candidate.channels)
                {
                    valid = false;
                    break;
                }
                candidate.mapping[count++] = static_cast<unsigned char>(index);
                if (!canonical.empty())
                    canonical += ',';
                canonical += std::to_string(index);
            }
            if (!valid || count != candidate.channels || !validFormat(candidate))
                continue;
            candidate.fmtp = "num_streams=" + std::to_string(candidate.streams) +
                             ";coupled_streams=" + std::to_string(candidate.coupled) +
                             ";channel_mapping=" + canonical +
                             ";maxaveragebitrate=" + std::to_string(candidate.bitrate()) +
                             ";useinbandfec=1";
        }
        else
            continue;
        if (candidate.channels > maximum || candidate.channels <= best.channels)
            continue;
        candidate.rtpmap = "a=rtpmap:" + std::to_string(pt) + " " + maps[pt];
        best = candidate;
    }
    if (!best.channels)
        return best;
    for (unsigned pt = 0; pt < 128; ++pt)
    {
        if (!offered[pt] || videoPayloads[pt] || static_cast<int>(pt) == best.payload ||
            lower(maps[pt]) != "red/48000/" + std::to_string(best.channels))
            continue;
        bool valid = !params[pt].empty();
        unsigned value = 0;
        for (const auto &item : split(params[pt], '/'))
            if (!integer(trim(item), value) || value != static_cast<unsigned>(best.payload))
                valid = false;
        if (!valid)
            continue;
        best.redPayload = static_cast<int>(pt);
        best.redRtpmap = "a=rtpmap:" + std::to_string(pt) + " " + maps[pt];
        best.redFmtp = "a=fmtp:" + std::to_string(pt) + " " + params[pt];
        break;
    }
    return best;
}
inline void outputFrame(const std::int16_t *input, unsigned channels, std::int16_t *output,
                        unsigned outputChannels)
{
    if (!output || outputChannels > 8)
        return;
    std::fill_n(output, outputChannels, 0);
    if (!input || (channels != 2 && channels != 6 && channels != 8) || channels > outputChannels)
        return;
    if (channels == 2)
    {
        output[0] = input[0];
        output[1] = input[1];
        return;
    }
    constexpr unsigned fiveOne[] = {0, 2, 1, 5, 3, 4};
    constexpr unsigned sevenOne[] = {0, 2, 1, 7, 5, 6, 3, 4};
    const auto *order = channels == 6 ? fiveOne : sevenOne;
    for (unsigned i = 0; i < channels; ++i)
        output[i] = input[order[i]];
}
} // namespace opennow::audio
