"""Builds the variants ffmpeg cannot write (ID3 flavours, MP4 box layouts, VBRI): v2.2, whole-tag unsynchronisation, extended
header, footer, per-frame unsync with a data length indicator, UTF-16BE, multi-value text,
numeric genres, ID3v1 fallback and leading junk. A .expect sidecar overrides ffprobe where
ffprobe is not a faithful oracle."""
import os
import struct
import sys

out = sys.argv[1]


def read(name):
    with open(os.path.join(out, name), "rb") as f:
        return f.read()


def write(name, data, expect=None):
    with open(os.path.join(out, name), "wb") as f:
        f.write(data)
    if expect:
        with open(os.path.join(out, name + ".expect"), "w", encoding="utf-8") as f:
            for k, v in expect.items():
                f.write("%s=%s\n" % (k, str(v).replace("\n", "\\n")))


def syncsafe(n):
    return bytes([(n >> 21) & 0x7F, (n >> 14) & 0x7F, (n >> 7) & 0x7F, n & 0x7F])


def unsync(b):
    o = bytearray()
    for i, x in enumerate(b):
        o.append(x)
        if x == 0xFF and (i + 1 == len(b) or b[i + 1] == 0 or b[i + 1] >= 0xE0):
            o.append(0)
    return bytes(o)


def enc(e, s, bom=True):
    if e == 0:
        return s.encode("latin-1")
    if e == 1:
        return (b"\xff\xfe" if bom else b"") + s.encode("utf-16-le")
    if e == 2:
        return s.encode("utf-16-be")
    return s.encode("utf-8")


def term(e):
    return b"\0\0" if e in (1, 2) else b"\0"


def text(e, *values):
    return bytes([e]) + term(e).join(enc(e, v) for v in values)


def frame(ver, fid, data, flags=0):
    if ver == 2:
        return fid.encode() + struct.pack(">I", len(data))[1:] + data
    size = struct.pack(">I", len(data)) if ver == 3 else syncsafe(len(data))
    return fid.encode() + size + struct.pack(">H", flags) + data


def tag(ver, body, flags=0, footer=False):
    head = b"ID3" + bytes([ver, 0, flags]) + syncsafe(len(body))
    return head + body + (b"3DI" + bytes([ver, 0, flags]) + syncsafe(len(body)) if footer else b"")


def apic(ver, mime, ptype, desc, img, e=0):
    if ver == 2:
        return bytes([e]) + mime.encode() + bytes([ptype]) + enc(e, desc) + term(e) + img
    return bytes([e]) + mime.encode() + b"\0" + bytes([ptype]) + enc(e, desc) + term(e) + img


png = read("cover.png")
jpg = read("cover.jpg")
audio = read("mp3_untagged.mp3")

# ID3v2.2
f = lambda i, d: frame(2, i, d)
body = b"".join([
    f("TT2", text(0, "Café Title")),
    f("TP1", text(1, "김뮤직")),
    f("TP2", text(0, "Album Artist")),
    f("TAL", text(1, "Écoute")),
    f("TCO", text(0, "(13)")),
    f("TYE", text(0, "2019")),
    f("TRK", text(0, "3/10")),
    f("TPA", text(0, "2/3")),
    f("TCM", text(1, "Ñandú")),
    f("ULT", bytes([1]) + b"eng" + enc(1, "") + term(1) + enc(1, "Lyrics 가사")),
    f("PIC", apic(2, "PNG", 3, "", png)),
])
write("id3v22.mp3", tag(2, body) + audio, {
    "title": "Café Title", "artist": "김뮤직", "albumArtist": "Album Artist", "album": "Écoute",
    "genre": "Pop", "year": 2019, "track": 3, "disc": 2, "composer": "Ñandú",
    "lyrics": "Lyrics 가사", "coverFile": "cover.png"})

# ID3v2.3, whole-tag unsync + extended header, two pictures (front cover is the second)
f = lambda i, d: frame(3, i, d)
ext = struct.pack(">I", 6) + b"\0\0" + struct.pack(">I", 0)
body = ext + b"".join([
    f("TIT2", text(1, "되돌이표")),
    f("TPE1", text(0, "Ünsync")),
    f("TALB", text(1, "Écoute")),
    f("TYER", text(0, "2019")),
    f("TCON", text(0, "(17)")),
    f("TRCK", text(0, "3/10")),
    f("TPOS", text(0, "1/2")),
    f("APIC", apic(3, "image/png", 0, "other", png)),
    f("APIC", apic(3, "image/jpeg", 3, "front", jpg)),
]) + b"\0" * 200
write("id3v23_unsync_ext.mp3", tag(3, unsync(body), 0xC0) + audio, {
    "title": "되돌이표", "artist": "Ünsync", "album": "Écoute", "genre": "Rock", "year": 2019,
    "track": 3, "disc": 1, "coverFile": "cover.jpg"})

# ID3v2.4: extended header, footer, UTF-16BE, UTF-8, multi-value, per-frame unsync + DLI
f = lambda i, d, fl=0: frame(4, i, d, fl)
ext = syncsafe(6) + b"\x01\x00"
img = unsync(jpg)
body = ext + b"".join([
    f("TIT2", text(3, "되돌이표")),
    f("TPE1", text(2, "김뮤직", "Zoë")),
    f("TCON", text(3, "(17)Hard Rock")),
    f("TDRC", text(3, "2019-05-02T10:20")),
    f("TRCK", text(3, "3/10")),
    f("TPOS", text(3, "1/2")),
    f("TXXX", text(3, "desc", "ignored")),
    f("USLT", bytes([3]) + b"kor" + b"\0" + "가사\n둘째 줄".encode()),
    f("APIC", syncsafe(len(apic(4, "image/jpeg", 3, "", jpg))) + unsync(apic(4, "image/jpeg", 3, "", jpg)), 0x03),
]) + b"\0" * 50
write("id3v24_ext_footer.mp3", tag(4, body, 0x50, footer=True) + audio, {
    "title": "되돌이표", "artist": "김뮤직; Zoë", "genre": "Hard Rock", "year": 2019, "track": 3,
    "disc": 1, "lyrics": "가사\n둘째 줄", "coverFile": "cover.jpg"})

# Numeric genre forms
for name, raw, want in [("genre_plain_number.mp3", "13", "Pop"), ("genre_multi.mp3", "(51)(39)", "Techno-Industrial; Noise"),
                        ("genre_remix.mp3", "(RX)", "Remix")]:
    body = frame(3, "TIT2", text(0, "t")) + frame(3, "TCON", text(0, raw))
    write(name, tag(3, body) + audio, {"title": "t", "genre": want})

# Year forms: Melon's TYER is YYMMDD; a year after next year is junk
for name, raw, want in [("year_yymmdd.mp3", "170324", 2017), ("year_yymmdd_1900s.mp3", "991231", 1999),
                        ("year_yyyymmdd.mp3", "20130213", 2013), ("year_junk.mp3", "20708744", 0)]:
    body = frame(3, "TIT2", text(0, "t")) + frame(3, "TYER", text(0, raw))
    write(name, tag(3, body) + audio, {"title": "t", "year": want})

# ID3v1 fills the fields ID3v2 leaves out
def fixed(s, n, codec="latin-1"):
    return s.encode(codec).ljust(n, b"\0")[:n]


def v1tag(title, artist, album, year, track, genre, codec="latin-1"):
    return (b"TAG" + fixed(title, 30, codec) + fixed(artist, 30, codec) + fixed(album, 30, codec) + year
            + fixed("", 28) + b"\0" + bytes([track, genre]))


v1 = v1tag("v1 title", "v1 artist", "v1 album", b"1999", 7, 17)
write("mp3_v2_plus_v1.mp3", tag(3, frame(3, "TIT2", text(0, "v2 title"))) + audio + v1, {
    "title": "v2 title", "artist": "v1 artist", "album": "v1 album", "year": 1999, "track": 7, "genre": "Rock"})

# Korean Windows taggers: CP949 in frames declared as Latin-1. "Ärzte" is a valid CP949
# (UHC) pair, "Größe" a Hanja pair and "Björk" no pair; all three stay Latin-1. ID3v1's
# 30 bytes cut the last Hangul syllable in half.
def raw(e, b):
    return bytes([e]) + b

body = (frame(3, "TIT2", raw(0, "비켜줄께".encode("cp949"))) + frame(3, "TPE1", raw(0, "브라운 아이드 소울".encode("cp949")))
        + frame(3, "TALB", text(0, "Ärzte")) + frame(3, "TPE2", text(0, "Größe")) + frame(3, "TCOM", text(0, "Björk")))
write("id3v23_cp949.mp3", tag(3, body) + audio, {
    "title": "비켜줄께", "artist": "브라운 아이드 소울", "album": "Ärzte", "albumArtist": "Größe", "composer": "Björk"})
write("id3v23_cp949_symbol.mp3", tag(3, frame(3, "TIT2", raw(0, "Chu~♡".encode("cp949")))) + audio, {"title": "Chu~♡"})
write("id3v1_cp949.mp3", audio + v1tag("우리 정말 사랑했어요 (With 케이윌)", "케이윌", "", b"2010", 9, 255, "cp949"), {
    "title": "우리 정말 사랑했어요 (With 케", "artist": "케이윌", "album": "", "year": 2010, "track": 9, "genre": ""})

# Hangul cut to its low 7 bits leaves control characters: the field is dropped.
body = frame(3, "TIT2", raw(0, b"\x18D(| (Feat. SGL\x08D)")) + frame(3, "TPE1", text(0, "artist"))
write("id3v23_broken_title.mp3", tag(3, body) + audio, {"title": "", "artist": "artist"})

# 12 TONE ANALYSIS in GEOB frames, as Music Center for PC writes them (a made-up analysis).
def smfmf_chunk(tag, payload):
    return tag + b"STAEMMLW" + bytes.fromhex("01008000") + struct.pack(">I", len(payload)) + payload

smfmf = (smfmf_chunk(b"GBPM", struct.pack(">f", 120.5)) + smfmf_chunk(b"STBF", bytes(range(0x20, 0x44)))
         + smfmf_chunk(b"STSA", struct.pack(">3I", 12345, 0, 12345))
         + smfmf_chunk(b"STMO", bytes.fromhex("01000040" "01050041" "01050142" "010a0043"))
         + smfmf_chunk(b"STHF", bytes.fromhex("08005000" "08035100" "08275200" "08285300"))
         + smfmf_chunk(b"STSA", struct.pack(">2I", 23456, 0)))
words = [0xFF] * 90
words[4:9], words[9:14] = [0x40] * 5, [0x41] * 5   # STMO 0x100, 0x105 (sub-index 0), each filling later types too
sthf_slot = (15, 16, 12, 1, 17, 4, 0, 13, 5, 7, 18, 10, 9, 19, 6, 8, 20, 21, 22, 2,
             23, 24, 25, 26, 27, 3, 28, 29, 30, 31, 32, 14, 33, 34, 35, 36, 11, 37, 38, 39)
for first, value in ((0, 0x50), (3, 0x51), (39, 0x52)):   # STHF 0x800, 0x803, 0x827, likewise
    for n in range(first, 40):
        words[14 + sthf_slot[n]] = value
words[54:90] = range(0x20, 0x44)
sensme = {"highlightMs": 23456, "sensme": bytes(words[4:]).hex()}   # the last STSA
geob = lambda e, mime, name, desc, data: bytes([e]) + mime + b"\0" + enc(e, name) + term(e) + enc(e, desc) + term(e) + data
body = (frame(3, "GEOB", geob(0, b"application/octet-stream", "", "other", b"\0" * 40))
        + frame(3, "GEOB", geob(0, b"application/SMFMF", "", "USR_SMFMF", smfmf))
        + frame(3, "GEOB", geob(0, b"application/SMFMF", "", "USR_SMFMF", smfmf_chunk(b"STSA", struct.pack(">2I", 1, 0)))))
write("id3v23_smfmf.mp3", tag(3, body + frame(3, "TIT2", text(0, "analyzed"))) + audio, dict(sensme, title="analyzed"))
write("id3v24_smfmf_utf16.mp3", tag(4, frame(4, "GEOB", geob(1, b"application/SMFMF", "a.smf", "USR_SMFMF", smfmf))) + audio,
      sensme)
# The device matches the MIME in any case and prefers a frame described USR_SMFMF; a frame
# with another description is taken only when there is none.
other = smfmf_chunk(b"STSA", struct.pack(">2I", 777, 0))
loose = geob(0, b"Application/smfmf", "", "analysis", smfmf + other)
write("id3v23_smfmf_loose.mp3", tag(3, frame(3, "GEOB", loose)) + audio, dict(sensme, highlightMs=777))
write("id3v23_smfmf_strict_later.mp3", tag(3, frame(3, "GEOB", loose)
                                           + frame(3, "GEOB", geob(0, b"application/SMFMF", "", "USR_SMFMF", smfmf))) + audio,
      sensme)

# Junk before the first frame, no tag at all
junk = bytes((i * 37 + 11) % 255 for i in range(1500))
write("mp3_junk_lead.mp3", junk + audio)

# ID3v2 in front of FLAC
flac = read("flac_16_44.flac")
write("flac_id3_lead.flac", tag(3, frame(3, "TXXX", text(0, "d", "v")) + b"\0" * 300) + flac)

# WAV with an id3 chunk after the data: its values win over LIST/INFO, INFO fills the rest
wav = bytearray(read("wav_16_info.wav"))
f = lambda i, d: frame(3, i, d)
body = b"".join([
    f("TIT2", text(1, "ID3 wins")),
    f("TPE1", text(0, "Ünsync")),
    f("TCOM", text(0, "Cömposer")),
    f("TPOS", text(0, "2/3")),
    f("USLT", bytes([0]) + b"eng" + b"\0" + b"wav lyrics"),
    f("APIC", apic(3, "image/png", 3, "", png)),
])
chunk = tag(3, body)
wav += b"id3 " + struct.pack("<I", len(chunk)) + chunk + (b"\0" if len(chunk) & 1 else b"")
wav[4:8] = struct.pack("<I", len(wav) - 8)
write("wav_id3_cover.wav", bytes(wav), {
    "title": "ID3 wins", "artist": "Ünsync", "album": "Écoute", "genre": "Rock", "year": 2019, "track": 3,
    "disc": 2, "composer": "Cömposer", "lyrics": "wav lyrics", "coverFile": "cover.png"})

# VBRI header (Fraunhofer) instead of Xing: 120 frames
vbri = bytearray(audio)
vbri[36:36 + 26] = b"VBRI" + struct.pack(">HHHII", 1, 0, 0, len(audio), 120) + struct.pack(">HHHH", 0, 1, 1, 1)
write("mp3_vbri.mp3", bytes(vbri))

# MP4 box layouts: 64-bit and to-end-of-file sizes, mdhd version 1, numeric genre
def parse_boxes(b):
    out, p = [], 0
    while p + 8 <= len(b):
        size, typ = struct.unpack(">I4s", b[p:p + 8])
        header = 8
        if size == 1:
            size, header = struct.unpack(">Q", b[p + 8:p + 16])[0], 16
        elif size == 0:
            size = len(b) - p
        out.append((typ, b[p + header:p + size]))
        p += size
    return out


def box(typ, payload):
    return struct.pack(">I4s", len(payload) + 8, typ) + payload


def rebuild(payload, edit):
    out = b""
    for typ, body in parse_boxes(payload):
        if typ in (b"moov", b"trak", b"mdia", b"udta", b"ilst"):
            body = rebuild(body, edit)
        elif typ == b"meta":
            body = body[:4] + rebuild(body[4:], edit)
        typ, body = edit(typ, body)
        out += box(typ, body)
    return out


def mdhd_v1(typ, body):
    if typ == b"mdhd" and body[0] == 0:
        ctime, mtime, scale, duration = struct.unpack(">IIII", body[4:20])
        return typ, bytes([1]) + body[1:4] + struct.pack(">QQIQ", ctime, mtime, scale, duration) + body[20:]
    if typ == b"\xa9gen":
        return b"gnre", box(b"data", struct.pack(">II", 0, 0) + struct.pack(">H", 18))
    return typ, body


m4a = read("m4a_aac.m4a")
boxes = parse_boxes(m4a)
assert boxes[-1][0] == b"moov"
head = b"".join(box(t, b) for t, b in boxes[:-1])
moov = boxes[-1][1]
write("m4a_moov64.m4a", head + struct.pack(">I4sQ", 1, b"moov", len(moov) + 16) + moov)
write("m4a_moov_size0.m4a", head + struct.pack(">I4s", 0, b"moov") + moov)
write("m4a_mdhd_v1_gnre.m4a", head + rebuild(box(b"moov", moov), mdhd_v1))

# 12 TONE ANALYSIS outside MP3: a FLAC APPLICATION block 'SMFM', and an MP4's moov/meta/ID32
# (version/flags, language, then an ID3v2 tag with the GEOB frame).
flac = read("flac_nocover_notags.flac")
assert flac[:4] == b"fLaC" and (flac[4] & 0x7F) == 0
info_end = 8 + 34
app = b"SMFM" + smfmf
last = flac[4] & 0x80
write("flac_smfmf.flac", b"fLaC" + bytes([flac[4] & 0x7F]) + flac[5:info_end]
      + bytes([2 | last]) + struct.pack(">I", len(app))[1:] + app + flac[info_end:], sensme)
id32 = box(b"ID32", b"\0\0\0\0" + b"\x55\xc4" + tag(3, frame(3, "GEOB", geob(0, b"application/SMFMF", "", "USR_SMFMF", smfmf))))
write("m4a_smfmf_id32.m4a", head + box(b"moov", moov + box(b"meta", b"\0\0\0\0" + id32)), sensme)

# ID3v1 alone, as ffmpeg writes it (UTF-8 bytes) and as Latin-1 taggers write it
write("mp3_v1only.mp3", audio + v1tag("되돌이표 (Remix)", "Zoë Müller & 김뮤직", "Écoute", b"2019", 3, 17, "utf-8"))
write("mp3_v1_latin1.mp3", audio + v1tag("Café", "Zoë Müller", "Écoute", b"2019", 3, 17), {
    "title": "Café", "artist": "Zoë Müller", "album": "Écoute", "genre": "Rock", "year": 2019, "track": 3})
