import ctypes
import dataclasses
import functools
import io
import itertools
import json
import math
import os
import re
import shutil
import struct
import time
import typing

import PIL.Image
import PIL.ImageDraw
import PIL.ImageFont
import fontTools.ttLib.ttFont as ttf

import sqexdata

SHOWCASE_TEXT = ("Uppercase: ABCDEFGHIJKLMNOPQRSTUVWXYZ\n"
                 "Lowercase: abcdefghijklmnopqrstuvwxyz\n"
                 "Numbers: 0123456789 ０１２３４５６７８９\n"
                 "SymbolsH: `~!@#$%^&*()_+-=[]{}\\|;':\",./<>?\n"
                 "SymbolsF: ｀～！＠＃＄％＾＆＊（）＿＋－＝［］｛｝￦｜；＇：＂，．／＜＞？\n"
                 "Hiragana: あかさたなはまやらわ\n"
                 "KatakanaH: ｱｶｻﾀﾅﾊﾏﾔﾗﾜ\n"
                 "KatakanaF: アカサタナハマヤラワ\n"
                 "Hangul: 가나다라마바사ㅇㅈㅊㅋㅌㅍㅎ\n"
                 "\n"
                 "<<SupportedUnicode>>\n"
                 "π™′＾¿¿‰øØ×∞∩£¥¢Ð€ªº†‡¤ ŒœŠšŸÅωψ↑↓→←⇔⇒♂♀♪¶§±＜＞≥≤≡÷½¼¾©®ª¹²³\n"
                 "※⇔｢｣«»≪≫《》【】℉℃‡。·••‥…¨°º‰╲╳╱☁☀☃♭♯✓〃¹²³\n"
                 "●◎○■□▲△▼▽∇♥♡★☆◆◇♦♦♣♠♤♧¶αß∇ΘΦΩδ∂∃∀∈∋∑√∝∞∠∟∥∪∩∨∧∫∮∬\n"
                 "∴∵∽≒≠≦≤≥≧⊂⊃⊆⊇⊥⊿⌒─━│┃│¦┗┓└┏┐┌┘┛├┝┠┣┤┥┫┬┯┰┳┴┷┸┻╋┿╂┼￢￣，－．／：；＜＝＞［＼］＿｀｛｜｝～＠\n"
                 "⑴⑵⑶⑷⑸⑹⑺⑻⑼⑽⑾⑿⒀⒁⒂⒃⒄⒅⒆⒇⓪①②③④⑤⑥⑦⑧⑨⑩⑪⑫⑬⑭⑮⑯⑰⑱⑲⑳\n"
                 "₀₁₂₃₄₅₆₇₈₉№ⅠⅡⅢⅣⅤⅥⅦⅧⅨⅩⅰⅱⅲⅳⅴⅵⅶⅷⅸⅹ０１２３４５６７８９！？＂＃＄％＆＇（）＊＋￠￤￥\n"
                 "ＡＢＣＤＥＦＧＨＩＪＫＬＭＮＯＰＱＲＳＴＵＶＷＸＹＺａｂｃｄｅｆｇｈｉｊｋｌｍｎｏｐｑｒｓｔｕｖｗｘｙｚ\n"
                 "\n"
                 "<<GameSpecific>>\n"
                 " \n"
                 "\n"
                 "\n"
                 "\n"
                 "\n"
                 "<<Kerning>>\n"
                 "AC AG AT AV AW AY LT LV LW LY TA Ta Tc Td Te Tg To VA Va Vc Vd Ve Vg Vm Vo Vp Vq Vu\n"
                 "A\0C A\0G A\0T A\0V A\0W A\0Y L\0T L\0V L\0W L\0Y T\0A T\0a T\0c T\0d T\0e T\0g T\0o V\0A V\0a V\0c V\0d V\0e V\0g V\0m V\0o V\0p V\0q V\0u\n"
                 "WA We Wq YA Ya Yc Yd Ye Yg Ym Yn Yo Yp Yq Yr Yu eT oT\n"
                 "W\0A W\0e W\0q Y\0A Y\0a Y\0c Y\0d Y\0e Y\0g Y\0m Y\0n Y\0o Y\0p Y\0q Y\0r Y\0u e\0T o\0T\n"
                 "Az Fv Fw Fy TV TW TY Tv Tw Ty VT WT YT tv tw ty vt wt yt\n"
                 "A\0z F\0v F\0w F\0y T\0V T\0W T\0Y T\0v T\0w T\0y V\0T W\0T Y\0T t\0v t\0w t\0y v\0t w\0t y\0t\n"
                 "\n"
                 "finish"
                 ).replace("\0", "\u200c")
HANGUL_CHARS_IN_UNICODE_BLOCKS = set(chr(x) for x in itertools.chain(
    range(0x1100, 0x1200),  # Hangul Jamo
    # range(0x3000, 0x3040),  # CJK Symbols and Punctuation
    range(0x3130, 0x3190),  # Hangul Compatibility Jamo
    # range(0x3200, 0x3300),  # Enclosed CJK Letters and Months
    # range(0x3400, 0x4DC0),  # CJK Unified Ideographs Extensions A
    # range(0x4E00, 0xA000),  # CJK Unified Ideographs
    range(0xA960, 0xA97F),  # Hangul Jamo Extended-A
    range(0xAC00, 0xD7B0),  # Hangul Syllables
    range(0xD7B0, 0xD800),  # Hangul Jamo Extended-B
    # range(0xF900, 0xFB00),  # CJK Compatibility Ideographs
    # range(0xFF00, 0xFFF0),  # Halfwidth and Fullwidth Forms
    # range(0x20000, 0x2A6E0),  # CJK Unified Ideographs Extensions B
    # range(0x2A700, 0x2B740),  # CJK Unified Ideographs Extensions C
    # range(0x2B740, 0x2B820),  # CJK Unified Ideographs Extensions D
    # range(0x2B820, 0x2CEB0),  # CJK Unified Ideographs Extensions E
    # range(0x2CEB0, 0x2EBF0),  # CJK Unified Ideographs Extensions F
    # range(0x2F800, 0x2FA20),  # CJK Compatibility Ideographs Supplement
))


class Canvas:
    _image: PIL.Image.Image
    _draw: PIL.ImageDraw.Draw

    def __init__(self, width: int, height: int, pix_fmt: str = "L"):
        self._image = PIL.Image.new(pix_fmt, (width, height))
        self._draw = PIL.ImageDraw.Draw(self._image)

        self.x = self.y = 0
        self.line_height = 0

    @property
    def draw(self) -> PIL.ImageDraw.Draw:
        return self._draw

    @property
    def image(self) -> PIL.Image.Image:
        return self._image


class Font:
    def getbbox(self, text: str) -> typing.Tuple[int, int, int, int]:
        raise NotImplementedError

    def getcharwidth(self, char: str) -> int:
        raise NotImplementedError

    def draw(self, canvas: Canvas, x: int, y: int, text: str, color: int = 255):
        raise NotImplementedError

    @property
    def name(self) -> str:
        raise NotImplementedError

    @property
    def size_pt(self) -> int:
        raise NotImplementedError

    @property
    def characters(self) -> typing.List[str]:
        raise NotImplementedError

    @property
    @functools.cache
    def max_boundary(self) -> typing.Tuple[int, int, int, int]:
        ml, mt, mr, mb = math.inf, math.inf, -math.inf, -math.inf
        for text in self.characters:
            l, t, r, b = self.getbbox(text)
            ml, mt, mr, mb = min(l, ml), min(t, mt), max(r, mr), max(b, mb)

        return int(ml), int(mt), int(mr), int(mb)

    @property
    def delta_y(self) -> int:
        return min(0, self.max_boundary[1])

    @property
    def font_height(self) -> int:
        return self.max_boundary[3] - self.delta_y

    @property
    def font_ascent(self) -> int:
        raise NotImplementedError

    @property
    def font_descent(self) -> int:
        raise NotImplementedError

    @property
    def kerning_info(self) -> typing.Dict[str, int]:
        raise NotImplementedError

    def draw_to_new_image(self, txt: str) -> PIL.Image.Image:
        l, t, r, b = self.getbbox(txt)
        w = r - l
        h = b - t

        canvas = Canvas(w, h, "RGB")
        canvas.draw.rectangle((-l, -t, w, h), outline=(0, 255, 0))
        self.draw(canvas, -l, -t, txt)
        return canvas.image


class SqexFont(Font):
    NEWLINE_SENTINEL = object()
    NONJOINER_SENTINEL = object

    def __init__(self, fdt_path: str, bitmap_paths: typing.List[typing.Union[PIL.Image.Image, str]]):
        self._fdt_path = fdt_path
        self._fdt = sqexdata.parse_fdt(fdt_path)
        self._bitmaps = []
        for path in bitmap_paths:
            if isinstance(path, str):
                with open(path, "rb") as fp:
                    if path.lower().endswith(".tex"):
                        fp: io.RawIOBase
                        fp.readinto(tex_header := sqexdata.TexHeader())
                        if tex_header.compression_type not in (0x1450, 0x1451, 0x1440):
                            continue
                        tex_header.mipmap_offsets = list(struct.unpack(
                            "<" + ("I" * tex_header.num_mipmaps),
                            fp.read(4 * tex_header.num_mipmaps)))
                        fp.seek(0)
                        data = fp.read()

                        assert len(tex_header.mipmap_offsets) == 1

                        i = 0
                        if tex_header.compression_type in (0x1440,):  # ARGB4444
                            img = sqexdata.ImageDecoding.rgba4444(data[tex_header.mipmap_offsets[i]:],
                                                                  int(tex_header.decompressed_width // pow(2, i)),
                                                                  int(tex_header.decompressed_height // pow(2, i)))
                            r, g, b, a = img.split()
                            self._bitmaps.extend((b, g, r, a))
                        else:
                            raise ValueError(f"File {path} has invalid compression_type: "
                                             f"0x{tex_header.compression_type:x}")
                    else:
                        im = PIL.Image.open(fp)
                        im.load()
                        self._bitmaps.append(im)
            elif isinstance(path, PIL.Image.Image):
                self._bitmaps.append(path)
            else:
                raise TypeError

        self._glyph_map: typing.Dict[str, sqexdata.FdtGlyphEntry] = {
            x.char: x for x in self._fdt.fthd_header.glyphs
        }
        self._kern_map: typing.Dict[str, int] = {
            f"{x.char1}{x.char2}": x.offset_x
            for x in self._fdt.knhd_header.entries
            if x.offset_x
        }
        self._max_glyph_text_length = max(len(x) for x in self._glyph_map.keys())

    @property
    def name(self) -> str:
        return os.path.basename(self._fdt_path).split(".")[0]

    @property
    def bitmaps(self):
        return self._bitmaps

    def getbbox(self, text: str) -> typing.Tuple[int, int, int, int]:
        return self.__draw(None, 0, 0, text)

    def getcharwidth(self, char: str) -> int:
        g = self._glyph_map[char]
        return g.width + g.offset_x

    @functools.cached_property
    def __min_offset_x(self):
        return min(x.offset_x for x in self._glyph_map.values())

    def __draw_single_glyph(self, canvas: typing.Optional[Canvas], x: int, y: int,
                            glyph: sqexdata.FdtGlyphEntry,
                            kerning: int, color: int = 255):
        x += kerning
        y += glyph.offset_y
        if canvas is not None:
            alpha = self._bitmaps[glyph.image_index].crop(
                (glyph.x, glyph.y, glyph.x + glyph.width, glyph.y + glyph.height))
            color = PIL.Image.new("L", alpha.size, color)
            canvas.image.paste(
                color,
                (x, y),
                alpha
            )
        return x, y, x + glyph.width, y + glyph.height

    def __draw(self, canvas: typing.Optional[Canvas], x: int, y: int, text: str, color: int = 255):
        split = []
        pos = 0
        while pos < len(text):
            schr = text[pos:pos + 1]
            if schr == '\n':
                split.append(SqexFont.NEWLINE_SENTINEL)
                pos += 1
                continue
            elif schr == '\u200c':
                split.append(SqexFont.NONJOINER_SENTINEL)
                pos += 1
                continue

            found = False
            for i in reversed(range(2, 1 + min(len(text) - pos, 1 + self._max_glyph_text_length))):
                if glyph := self._glyph_map.get(text[pos:pos + i], None):
                    split.append(glyph)
                    pos += i
                    found = True
                    break
            if found:
                continue

            if glyph := self._glyph_map.get(schr, None):
                split.append(glyph)
            else:
                split.append(self._glyph_map['=' if "=" in self._glyph_map else "!"])
            pos += 1

        if len(split) == 1:
            if split[0] in (SqexFont.NONJOINER_SENTINEL, SqexFont.NEWLINE_SENTINEL):
                return x, y, x, y
            return self.__draw_single_glyph(canvas, x, y, split[0], 0, color)

        last_char = ""
        left, top, right, bottom = x, y, x, y + self.font_height
        cur_x = x
        cur_y = y
        for glyph in split:
            if glyph is SqexFont.NEWLINE_SENTINEL:
                cur_x = x
                cur_y += self.font_height
                bottom += self.font_height
                last_char = ""
                continue
            elif glyph is SqexFont.NONJOINER_SENTINEL:
                last_char = ""
                continue

            current_char = glyph.char

            kerning = self._kern_map.get(last_char + current_char, 0)
            now_l, now_t, now_r, now_b = self.__draw_single_glyph(canvas, cur_x, cur_y, glyph, kerning, color)

            left = min(left, now_l)
            right = max(right, now_r)

            cur_x = now_r + glyph.offset_x
            last_char = current_char

        return left, top, right, bottom

    def draw(self, canvas: Canvas, x: int, y: int, text: str, color: int = 255):
        self.__draw(canvas, x, y, text, color)

    @property
    def size_pt(self) -> int:
        return int(round(self._fdt.fthd_header.size))

    @property
    @functools.cache
    def characters(self) -> typing.List[str]:
        return list(sorted(self._glyph_map.keys()))

    @property
    def font_height(self) -> int:
        return self._fdt.fthd_header.font_height

    @property
    def font_ascent(self) -> int:
        return self._fdt.fthd_header.font_ascent

    @property
    def font_descent(self) -> int:
        return self._fdt.fthd_header.font_height - self._fdt.fthd_header.font_ascent

    @property
    def kerning_info(self) -> typing.Dict[str, int]:
        return self._kern_map


class FreeTypeFont(Font):
    def __init__(self, path: str, font_index: int, size: int):
        self._path = path
        self._font_index = font_index
        self._size = size
        self._ttfont = ttf.TTFont(path, fontNumber=font_index)
        self._pilfont: PIL.ImageFont.FreeTypeFont = PIL.ImageFont.FreeTypeFont(path, size=size, index=font_index)
        self._character_map = {chr(itemord): itemname
                               for x in self._ttfont["cmap"].tables
                               for itemord, itemname in x.cmap.items()
                               if "glyf" not in self._ttfont or itemname in self._ttfont["glyf"].glyphs
                               }

        self._name_to_ords = {}
        for k, v in self._character_map.items():
            if v not in self._name_to_ords:
                self._name_to_ords[v] = [k]
            else:
                self._name_to_ords[v].append(k)
        pass

    def getbbox(self, text: str) -> typing.Tuple[int, int, int, int]:
        return self._pilfont.getbbox(text)

    def getcharwidth(self, char: str) -> int:
        return round(self._ttfont.getGlyphSet()[self._character_map[char]].width
                     * float(self._size)
                     / self._ttfont['head'].unitsPerEm)

    def draw(self, canvas: Canvas, x: int, y: int, text: str, color: int = 255):
        canvas.draw.text((x, y), text, font=self._pilfont, fill=color, anchor="la")

    @property
    def name(self) -> str:
        return str(self._ttfont["name"].names[1])

    @property
    def size_pt(self) -> int:
        return self._size

    @property
    @functools.cache
    def characters(self) -> typing.List[str]:
        return list(sorted(self._character_map.keys()))

    @property
    def font_height(self) -> int:
        return self._pilfont.font.ascent + self._pilfont.font.descent

    @property
    def font_ascent(self) -> int:
        return self._pilfont.font.ascent - self.delta_y

    @property
    def font_descent(self) -> int:
        return self._pilfont.font.descent

    @property
    def kerning_info(self) -> typing.Dict[str, int]:
        data = self._ttfont.get("kern", None)
        if not data:
            return {}
        result = {}
        for kern_table in data.kernTables:
            for (glyph1_name, glyph2_name), distance in kern_table.kernTable.items():
                distance = int(round(distance
                                     * float(self._size)
                                     / self._ttfont['head'].unitsPerEm))
                if not distance:
                    continue
                for glyph1_char in self._name_to_ords.get(glyph1_name, []):
                    for glyph2_char in self._name_to_ords.get(glyph2_name, []):
                        result[f"{glyph1_char}{glyph2_char}"] = distance
                        pass

        return result


def create_fdt(plan: 'FontTransformPlan') -> bytes:
    use_chars = set(plan.chars)

    font_table_header = sqexdata.FdtFontTableHeader()
    font_table_header.signature = sqexdata.FdtFontTableHeader.SIGNATURE
    font_table_header.null_1 = 0
    font_table_header.image_width = plan.canvas_width
    font_table_header.image_height = plan.canvas_height
    font_table_header.size = plan.source_font.size_pt
    font_table_header.font_ascent = plan.source_font.font_ascent
    font_table_header.font_height = plan.source_font.font_height

    charmap: typing.Dict[str, Font] = {}
    for i, f in enumerate(plan.fonts):
        for text in f.characters:
            if text not in charmap and (use_chars is None or text in use_chars):
                charmap[text] = f
    charmap = {x: charmap[x] for x in sorted(charmap.keys())}
    font_table_header.glyph_count = len(charmap)

    left_offset = 0
    min_shift_y = 0
    for text, font in charmap.items():
        actual_render_text = '.' if text == '\n' else text
        shift_x, shift_y, *_ = font.getbbox(actual_render_text)
        left_offset = max(-shift_x, left_offset)
        min_shift_y = min(min_shift_y, shift_y)

    left_offset = max(min(left_offset, plan.source_font.size_pt // 4, plan.max_left_offset), plan.min_left_offset)

    """
    keep distance between mid and ascent consistent.
    """

    # key: text
    glyphs: typing.Dict[str, sqexdata.FdtGlyphEntry] = {}

    last_print = 0
    for text_index, (text, font) in enumerate(charmap.items()):
        now = time.time()
        if now - last_print > 0.1:
            print(f"\rCanvas #{len(plan.canvases)}: [{text_index + 1}/{len(charmap)}] {text}...", end="")
            last_print = now

        glyph = sqexdata.FdtGlyphEntry()
        glyph.char = text

        actual_render_text = '.' if text == '\n' else text

        shift_x, _, glyph.width, bottom = font.getbbox(actual_render_text)
        glyph.width = glyph.width - shift_x + left_offset
        glyph.height = max(font_table_header.font_height, font.font_height) - min_shift_y
        glyph.offset_x = font.getcharwidth(text) - glyph.width
        glyph.offset_y = (round(font_table_header.font_height / 2 - font.font_height / 2)
                          + min_shift_y
                          + plan.global_offset_y_modifier)
        line_height = glyph.height

        if rendered := plan.rendered_glyphs.get((font.name, font.size_pt, text), None):
            glyph.image_index, glyph.x, glyph.y = rendered
        else:
            new_canvas = False
            if not plan.canvases:
                new_canvas = True
            elif plan.canvases[-1].x + glyph.width >= plan.canvases[-1].image.width - plan.distance_between_glyphs:
                plan.canvases[-1].x = plan.distance_between_glyphs
                plan.canvases[-1].y += plan.canvases[-1].line_height + plan.distance_between_glyphs
                plan.canvases[-1].line_height = 0
                if plan.canvases[-1].y + line_height + plan.distance_between_glyphs >= plan.canvases[-1].image.height:
                    new_canvas = True

            if new_canvas:
                plan.canvases.append(Canvas(plan.canvas_width, plan.canvas_height))
                plan.canvases[-1].x = plan.canvases[-1].y = plan.distance_between_glyphs

            glyph.image_index = len(plan.canvases) - 1

            glyph.x, glyph.y = plan.canvases[-1].x, plan.canvases[-1].y
            font.draw(plan.canvases[-1],
                      glyph.x + left_offset,
                      glyph.y - min_shift_y,
                      actual_render_text)

            plan.canvases[-1].x += glyph.width + plan.distance_between_glyphs
            plan.canvases[-1].line_height = max(plan.canvases[-1].line_height, line_height)

        glyphs[text] = glyph
        plan.rendered_glyphs[font, font.size_pt, text] = glyph.image_index, glyph.x, glyph.y

    kerning_header = sqexdata.FdtKerningHeader()
    kerning_header.signature = sqexdata.FdtKerningHeader.SIGNATURE

    header = sqexdata.FdtHeader()
    header.signature = sqexdata.FdtHeader.SIGNATURE
    header.version = sqexdata.FdtHeader.VERSION
    header.glyph_header_offset = ctypes.sizeof(header)
    header.knhd_header_offset = (ctypes.sizeof(header)
                                 + ctypes.sizeof(font_table_header)
                                 + len(glyphs) * ctypes.sizeof(sqexdata.FdtGlyphEntry))

    filtered_kerning_data = {}
    for i, font in enumerate(plan.fonts):
        for kerning_str, offset_x in font.kerning_info.items():
            offset_x = round(offset_x)
            if len(kerning_str) != 2:
                # Unsupported
                continue

            c1, c2 = kerning_str
            if c1 not in charmap or c2 not in charmap:
                continue
            if charmap[c1] != i and charmap[c2] != i:
                # Completely unrelated; ignore
                continue
            if charmap[c1] != charmap[c2]:
                if (c1 not in plan.kerning_treat_same_font_characters
                        and c2 not in plan.kerning_treat_same_font_characters):
                    # Different font; ignore
                    continue
            filtered_kerning_data[c1, c2] = offset_x

    kernings = []
    filtered_kerning_data = {x: filtered_kerning_data[x]
                             for x in sorted(filtered_kerning_data.keys())}
    font_table_header.kerning_entry_count = kerning_header.count = len(filtered_kerning_data)
    for (c1, c2), offset_x in filtered_kerning_data.items():
        entry = sqexdata.FdtKerningEntry()
        entry.char1 = c1
        entry.char2 = c2
        entry.offset_x = offset_x
        kernings.append(entry)

    fp = io.BytesIO()
    fp.write(header)
    fp.write(font_table_header)
    for entry in glyphs.values():
        fp.write(entry)
    fp.write(kerning_header)
    for entry in kernings:
        fp.write(entry)

    print()
    return fp.getvalue()


@dataclasses.dataclass
class SqFontSet:
    fdt_filename_pattern: re.Pattern
    tex_filename_left: str
    tex_filename_right: str
    fdt_files: typing.List[str] = dataclasses.field(default_factory=list)
    tex_files: typing.List[str] = dataclasses.field(default_factory=list)
    fonts: typing.Dict[str, SqexFont] = dataclasses.field(default_factory=dict)
    bitmaps: typing.Optional[typing.List[PIL.Image.Image]] = None
    result_canvases: typing.List[Canvas] = dataclasses.field(default_factory=list)


@dataclasses.dataclass
class FontChainConfig:
    add_hangul: bool = True
    add_chars: str = ""
    chain: typing.List[str] = dataclasses.field(default_factory=lambda: [None])


@dataclasses.dataclass
class SqFontTransformConfig:
    root_dir: str
    texture_width: int = 4096
    texture_height: int = 4096
    full_height_every_char: bool = True
    external_freetype_fonts: typing.Dict[str, typing.Tuple[str, int]] = dataclasses.field(default_factory=lambda: {
        "gulim": (r"C:\Windows\Fonts\gulim.ttc", 0),
        "gungsuh": (r"C:\Windows\Fonts\batang.ttc", 2),
        "times": (r"C:\Windows\Fonts\times.ttf", 0),
        "times bold": (r"C:\Windows\Fonts\timesbd.ttf", 0),
        "comic": (r"C:\Windows\Fonts\comic.ttf", 0),
        "papyrus": (r"C:\Windows\Fonts\PAPYRUS.TTF", 0),
        "impact": (r"C:\Windows\Fonts\impact.ttf", 0),
        "wide latin": (r"C:\Windows\Fonts\LATINWD.TTF", 0),
        "chiller": (r"C:\Windows\Fonts\CHILLER.TTF", 0),
        "elliotsix": (r"fonts\ElliotSix.ttf", 0),
        "noto medium": (r"fonts\NotoSansCJKkr-hinted\NotoSansCJKkr-Medium.otf", 0),
        "noto regular": (r"fonts\NotoSansCJKkr-hinted\NotoSansCJKkr-Regular.otf", 0),
        "source medium": (r"fonts\SourceHanSansKorean\SourceHanSansK-Medium.otf", 0),
        "source normal": (r"fonts\SourceHanSansKorean\SourceHanSansK-Normal.otf", 0),
        "source regular": (r"fonts\SourceHanSansKorean\SourceHanSansK-Regular.otf", 0),
    })
    fallback_fonts: typing.List[str] = dataclasses.field(default_factory=lambda: [
        "gungsuh",
        "source medium",
    ])

    axis: FontChainConfig = dataclasses.field(default_factory=lambda: FontChainConfig(
        chain=["comic", "gungsuh", None],
        add_hangul=True,
    ))
    jupiter_latin: FontChainConfig = dataclasses.field(default_factory=lambda: FontChainConfig(
        chain=["comic", "gungsuh", None],
        add_hangul=True,
    ))
    jupiter_number: FontChainConfig = dataclasses.field(default_factory=lambda: FontChainConfig(
        chain=["comic", "gungsuh", None],
        add_hangul=False,
    ))
    miedingermid: FontChainConfig = dataclasses.field(default_factory=lambda: FontChainConfig(
        chain=["comic", "gungsuh", None],
        add_hangul=True,
    ))
    meidinger: FontChainConfig = dataclasses.field(default_factory=lambda: FontChainConfig(
        chain=["comic", "gungsuh", None],
        add_hangul=False,
    ))
    trumpgothic: FontChainConfig = dataclasses.field(default_factory=lambda: FontChainConfig(
        chain=["comic", "gungsuh", None],
        add_hangul=True,
    ))


@dataclasses.dataclass
class FontTransformPlan:
    source_font: SqexFont

    canvases: typing.List[Canvas]

    # (component font name, size, glyph): (image index, x, y)
    rendered_glyphs: typing.Dict[typing.Tuple[str, int, str], typing.Tuple[int, int, int]]

    fonts: typing.List[Font]

    chars: typing.List[str]

    canvas_width: int
    canvas_height: int
    kerning_treat_same_font_characters: str = " "
    distance_between_glyphs: int = 1
    global_offset_y_modifier: int = -1
    min_left_offset: int = 0
    max_left_offset: int = 4


opened_freetype_fonts = {}


def open_freetype_font(path_and_index: typing.Tuple[str, int], size: int):
    key = *path_and_index, size
    font = opened_freetype_fonts.get(key, None)
    if font is None:
        font = FreeTypeFont(path_and_index[0], path_and_index[1], size)
        opened_freetype_fonts[key] = font
    return font


def transform_sqex(config: SqFontTransformConfig,
                   preview_characters_per_line: int = 128):
    """
    * AXIS: default font
    * KrnAXIS: default font for Korean clients only
    * Jupiter: Serif font
        * 16, 20, 23, 46: Latin characters
            * Data center selection
                * Server names (ex. Adamantoise)
                * Data center group names (ex. North American Data Center)
            * Job names
        * 45, 90: Damage numbers (0123456789!)
    * MiedingerMid: Wide font
        * Login menu text (ex. START)
    * Meidinger (sic): Numeric stats (!%+-./0123456789?)
    * TrumpGothic: Narrow font
        * Data center selection
            * Data center names (ex. Aether)
        * Window titles
    """

    source_fonts = {
        "lobby": SqFontSet(re.compile(r".*_lobby\.fdt", re.IGNORECASE), "font_lobby", ".tex"),
        "krn": SqFontSet(re.compile(r"KrnAXIS_.*\.fdt", re.IGNORECASE), "font_krn_", ".tex"),
        "common": SqFontSet(re.compile(r".*\.fdt", re.IGNORECASE), "font", ".tex"),
    }
    root_dir = os.path.realpath(config.root_dir)
    for filename in sorted(os.listdir(os.path.join(root_dir, "source"))):
        full_path = os.path.join(root_dir, "source", filename)
        if os.stat(full_path).st_size == 0:  # placeholder entry
            continue
        for font_set in source_fonts.values():
            if font_set.fdt_filename_pattern.fullmatch(filename):
                font_set.fdt_files.append(full_path)
                break
            if (filename.startswith(font_set.tex_filename_left) and filename.endswith(font_set.tex_filename_right)
                    and filename[len(font_set.tex_filename_left):-len(font_set.tex_filename_right)].isdecimal()):
                font_set.tex_files.append(full_path)
                break

    for font_group, font_set in source_fonts.items():
        preview_dir = os.path.join(root_dir, f"preview_{font_group}")
        os.makedirs(preview_dir, exist_ok=True)
        for fdt_file in font_set.fdt_files:
            font = SqexFont(fdt_file, font_set.bitmaps or font_set.tex_files)
            font_set.fonts[font.name] = font
            font_set.bitmaps = font.bitmaps
            preview_path = os.path.join(preview_dir, f"{font.name}.png")
            if not os.path.exists(preview_path):
                preview_text = "\n".join("".join(font.characters[i:i + preview_characters_per_line])
                                         for i in range(0, len(font.characters), preview_characters_per_line))
                preview = font.draw_to_new_image(preview_text)
                preview.save(preview_path)

    result_dir = os.path.join(root_dir, f"result")
    shutil.rmtree(result_dir, ignore_errors=True)
    os.makedirs(result_dir, exist_ok=True)
    for font_group, font_set in source_fonts.items():
        rendered_glyphs = {}
        result_fdts = []
        preview_dir = os.path.join(root_dir, f"preview_{font_group}_result")
        shutil.rmtree(preview_dir, ignore_errors=True)
        os.makedirs(preview_dir, exist_ok=True)
        for i, source_font in enumerate(font_set.fonts.values()):
            print(f"[{i}/{len(font_set.fonts)}] Working on {font_group} > {source_font.name}...")
            plan = FontTransformPlan(
                source_font=source_font,
                canvases=font_set.result_canvases,
                rendered_glyphs=rendered_glyphs,
                fonts=[],
                chars=list(source_font.characters),
                canvas_width=config.texture_width,
                canvas_height=config.texture_height,
            )
            if "axis" in source_font.name.lower():
                config2 = config.axis
            elif any(x in source_font.name.lower() for x in (
                    "jupiter_16",
                    "jupiter_20",
                    "jupiter_23",
                    "jupiter_46",
            )):
                config2 = config.jupiter_latin
            elif any(x in source_font.name.lower() for x in (
                    "jupiter_45",
                    "jupiter_90",
            )):
                config2 = config.jupiter_number
            elif "meidinger" in source_font.name.lower():
                config2 = config.meidinger
            elif "miedingermid" in source_font.name.lower():
                config2 = config.miedingermid
            elif "trumpgothic" in source_font.name.lower():
                config2 = config.trumpgothic
            else:
                print("Skipping", source_font.name)
                continue
            already = set()
            for font_name in [*config2.chain, *config.fallback_fonts]:
                if font_name in already:
                    continue
                already.add(font_name)
                plan.fonts.append(
                    source_font
                    if font_name is None else
                    open_freetype_font(config.external_freetype_fonts[font_name], round(source_font.size_pt)))
            if None not in already:
                plan.fonts.append(source_font)
            if config2.add_hangul:
                plan.chars.extend(HANGUL_CHARS_IN_UNICODE_BLOCKS)
            plan.chars.extend(config2.add_chars)
            result_fdt = os.path.join(result_dir, f"{source_font.name}.fdt")
            result_fdts.append(result_fdt)
            with open(result_fdt, "wb") as fp:
                fp.write(create_fdt(plan))

        print("Saving texture files...")
        for _ in range(len(font_set.result_canvases), (len(font_set.result_canvases) + 3) // 4 * 4):
            font_set.result_canvases.append(Canvas(config.texture_width, config.texture_height))

        tex_header = sqexdata.TexHeader()
        tex_header.header_size = 0x80
        tex_header.compression_type = 0x1440
        tex_header.decompressed_width = config.texture_width
        tex_header.decompressed_height = config.texture_height
        tex_header.depth = 1
        tex_header.num_mipmaps = 1

        for i in range(0, len(font_set.result_canvases), 4):
            b, g, r, a = tuple(x.image for x in font_set.result_canvases[i:i + 4])
            img: PIL.Image.Image = PIL.Image.merge("RGBA", (r, g, b, a))
            img_bytes = sqexdata.ImageEncoding.rgba4444(img)
            # img_bytes = sqexdata.ImageEncoding.bgra(img)
            tex_filename = f"{font_set.tex_filename_left}{1 + (i // 4)}{font_set.tex_filename_right}"
            with open(os.path.join(result_dir, tex_filename), "wb") as fp:
                fp: typing.Union[typing.BinaryIO, io.RawIOBase]
                fp.write(tex_header)
                fp.write(b"\x50")
                fp.seek(0x50, os.SEEK_SET)
                fp.write(img_bytes)

        print("Generating previews...")
        for i, c in enumerate(font_set.result_canvases):
            c.image.save(os.path.join(preview_dir, f"texture_{i}.png"))

        for result_fdt in result_fdts:
            font = SqexFont(result_fdt, [x.image for x in font_set.result_canvases])
            preview_path = os.path.join(preview_dir, f"{font.name}.png")
            if not os.path.exists(preview_path):
                preview_text = "\n".join("".join(font.characters[i:i + preview_characters_per_line])
                                         for i in range(0, len(font.characters), preview_characters_per_line))
                preview = font.draw_to_new_image(preview_text)
                preview.save(preview_path)


def __main__():
    root_dir = r"z:\scratch\sqex_fonts"
    return transform_sqex(SqFontTransformConfig(root_dir=root_dir))


if __name__ == "__main__":
    exit(__main__())
