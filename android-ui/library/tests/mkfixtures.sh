#!/usr/bin/env bash
# Generates the tagreader fixtures with ffmpeg, plus the hand-built ID3 variants from mkid3.py.
set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
out=${1:-$here/fixtures}
rm -rf "$out"
mkdir -p "$out"
cd "$out"

ff() { ffmpeg -hide_banner -loglevel error -y "$@"; }

ff -f lavfi -i "testsrc2=size=321x243:rate=1" -frames:v 1 cover.png
ff -f lavfi -i "testsrc=size=401x299:rate=1" -frames:v 1 -q:v 3 cover.jpg
ff -f lavfi -i "color=c=gray:size=400x401,noise=alls=100:allf=t+u" -frames:v 1 big_cover.png

# ffmpeg's Ogg muxer has no attached-picture support: carry the picture as METADATA_BLOCK_PICTURE.
python3 -I - <<'PY'
import base64, struct

def ffmeta(image, mime, name):
    data = open(image, "rb").read()
    block = (struct.pack(">II", 3, len(mime)) + mime + struct.pack(">I", 0) + struct.pack(">IIII", 0, 0, 0, 0)
             + struct.pack(">I", len(data)) + data)
    value = base64.b64encode(block).decode().replace("=", "\\=")
    open(name, "w").write(";FFMETADATA1\nMETADATA_BLOCK_PICTURE=" + value + "\n")

ffmeta("cover.jpg", b"image/jpeg", "pic_small.ffmeta")
ffmeta("big_cover.png", b"image/png", "pic_big.ffmeta")
PY

SINE=(-f lavfi -i "sine=frequency=440:sample_rate=44100:duration=6")
SINE9=(-f lavfi -i "sine=frequency=440:sample_rate=44100:duration=9")
SINE96=(-f lavfi -i "sine=frequency=440:sample_rate=96000:duration=6")
SINE48=(-f lavfi -i "sine=frequency=440:sample_rate=48000:duration=6")
SINE22=(-f lavfi -i "sine=frequency=440:sample_rate=22050:duration=7")
NOISE=(-f lavfi -i "anoisesrc=color=pink:amplitude=0.3:sample_rate=44100:duration=10:seed=7")
ST=(-ac 2)

TITLE="되돌이표 (Remix)"
ARTIST="Zoë Müller & 김뮤직"
ALBUM="Écoute"
ALBUMARTIST="Various Artists 합창단"
COMPOSER="Ñandú Görges"
LYRICS=$'첫 줄 Café\nSecond line'
meta=(-metadata "title=$TITLE" -metadata "artist=$ARTIST" -metadata "album=$ALBUM"
    -metadata "album_artist=$ALBUMARTIST" -metadata "composer=$COMPOSER" -metadata "genre=Rock"
    -metadata "date=2019-05-02" -metadata "track=3/10" -metadata "disc=1/2" -metadata "lyrics=$LYRICS")
pic() { PIC=(-i "$1" -map 0:a -map 1:v -c:v copy -disposition:v attached_pic); [ -z "${2:-}" ] || PIC+=(-metadata:s:v:0 "comment=Cover (front)"); }

# FLAC: ffmpeg's own key names (track, disc, album_artist), then the standard Vorbis spellings.
pic cover.png
ff "${SINE[@]}" "${PIC[@]}" "${ST[@]}" -c:a flac -sample_fmt s16 "${meta[@]}" flac_16_44.flac
pic cover.jpg front
ff "${SINE96[@]}" "${PIC[@]}" "${ST[@]}" -c:a flac -sample_fmt s32 -bits_per_raw_sample 24 \
    -metadata "TITLE=$TITLE" -metadata "ARTIST=$ARTIST" -metadata "ALBUM=$ALBUM" \
    -metadata "ALBUMARTIST=$ALBUMARTIST" -metadata "COMPOSER=$COMPOSER" -metadata "GENRE=Jazz" \
    -metadata "DATE=2019" -metadata "TRACKNUMBER=3/10" -metadata "DISCNUMBER=1" \
    -metadata "UNSYNCEDLYRICS=$LYRICS" flac_24_96.flac
ff "${SINE[@]}" -c:a flac -map_metadata -1 flac_nocover_notags.flac

# MP3
pic cover.jpg front
ff "${SINE9[@]}" "${PIC[@]}" "${ST[@]}" -c:a libmp3lame -b:a 320k -id3v2_version 4 -write_id3v1 0 "${meta[@]}" mp3_cbr320_v24.mp3
pic cover.png
ff "${NOISE[@]}" "${PIC[@]}" "${ST[@]}" -c:a libmp3lame -q:a 2 -id3v2_version 3 -write_id3v1 1 "${meta[@]}" mp3_vbr_v23_v1.mp3
ff "${SINE9[@]}" "${ST[@]}" -c:a libmp3lame -b:a 128k -write_xing 0 -map_metadata -1 -id3v2_version 0 -write_id3v1 0 mp3_untagged.mp3
ff "${SINE22[@]}" -c:a libmp3lame -b:a 64k -ac 1 -id3v2_version 3 -write_id3v1 0 "${meta[@]}" mp3_mpeg2_mono.mp3

ff "${SINE9[@]}" -ar 11025 -c:a libmp3lame -b:a 32k -map_metadata -1 -id3v2_version 0 mp3_mpeg25_11k.mp3
ff "${SINE9[@]}" "${ST[@]}" -c:a mp2 -b:a 192k -map_metadata -1 -id3v2_version 0 mp2_layer2.mp2

# Ogg
ff "${SINE[@]}" "${ST[@]}" -c:a libvorbis -q:a 4 "${meta[@]}" ogg_vorbis.ogg
ff "${SINE[@]}" -i pic_big.ffmeta -map_metadata 1 "${ST[@]}" -c:a libvorbis -q:a 4 "${meta[@]}" ogg_vorbis_bigcover.ogg
ff "${SINE48[@]}" "${ST[@]}" -c:a libopus -b:a 96k -ar 48000 "${meta[@]}" opus.opus
ff "${SINE48[@]}" -i pic_small.ffmeta -map_metadata 1 "${ST[@]}" -c:a libopus -b:a 96k -ar 48000 "${meta[@]}" opus_cover.opus

# MP4
pic cover.jpg
ff "${SINE[@]}" "${PIC[@]}" "${ST[@]}" -c:a aac -b:a 128k "${meta[@]}" m4a_aac.m4a
ff "${SINE96[@]}" "${ST[@]}" -c:a aac -b:a 128k "${meta[@]}" m4a_aac_nocover_96k.m4a
pic cover.png
ff "${SINE[@]}" "${PIC[@]}" "${ST[@]}" -c:a alac -sample_fmt s32p -bits_per_raw_sample 24 "${meta[@]}" m4a_alac_24.m4a
ff "${SINE96[@]}" "${ST[@]}" -c:a alac -sample_fmt s32p -bits_per_raw_sample 24 -map_metadata -1 m4a_alac_24_96k.m4a
ff "${SINE[@]}" "${ST[@]}" -c:a alac -sample_fmt s16p -map_metadata -1 m4a_alac_16.m4a

# WAV / AIFF
ff "${SINE[@]}" "${ST[@]}" -c:a pcm_s16le "${meta[@]}" wav_16_info.wav
ff "${SINE96[@]}" -c:a pcm_s24le "${meta[@]}" wav_24_96_extensible.wav
ff "${SINE[@]}" "${ST[@]}" -c:a pcm_f32le -map_metadata -1 wav_float32.wav
ff "${SINE[@]}" -c:a pcm_s16be -f aiff -metadata "title=$TITLE" -metadata "author=$ARTIST" aiff_plain.aiff
ff "${SINE[@]}" "${ST[@]}" -c:a pcm_s16le -f aiff -map_metadata -1 aifc_sowt.aifc
ff "${SINE96[@]}" -ar 88200 "${ST[@]}" -c:a pcm_s24be -f aiff -map_metadata -1 aiff_24_88k.aiff
pic cover.jpg front
ff "${SINE[@]}" "${PIC[@]}" "${ST[@]}" -c:a pcm_s16be -f aiff -write_id3v2 1 "${meta[@]}" aiff_id3.aiff

# Damaged inputs
head -c 1000 flac_16_44.flac > bad_truncated.flac
head -c 3000 ogg_vorbis_bigcover.ogg > bad_truncated.ogg
head -c 400 m4a_aac.m4a > bad_truncated.m4a
head -c $(($(stat -c%s m4a_aac.m4a) - 100)) m4a_aac.m4a > bad_truncated_moov.m4a
head -c 30 wav_16_info.wav > bad_truncated.wav
python3 -I - <<'PY'
import random
open("bad_random.bin", "wb").write(random.Random(1).randbytes(20000))
open("bad_text.mp3", "wb").write(b"this is not audio\n" * 400)
open("bad_empty.mp3", "wb").close()
PY

python3 -I "$here/mkid3.py" "$out"
echo "fixtures in $out"
