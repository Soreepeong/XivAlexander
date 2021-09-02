import ctypes
import functools
import io
import itertools
import math
import os
import shutil
import struct
import typing

import PIL.Image
import PIL.ImageDraw
import PIL.ImageFont
import fontTools.ttLib.ttFont as ttf

import sqexdata

# https://docs.microsoft.com/en-us/typography/opentype/spec/features_ko
# kern, dist


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


class Canvas:
    _image: PIL.Image.Image
    _draw: PIL.ImageDraw.Draw

    def __init__(self, width: int, height: int):
        self._image = PIL.Image.new("L", (width, height))
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

    def draw(self, canvas: Canvas, x: int, y: int, text: str):
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


class SqexFont(Font):
    NEWLINE_SENTINEL = object()
    NONJOINER_SENTINEL = object

    def __init__(self, fdt_path: str, bitmap_paths: typing.List[typing.Union[PIL.Image.Image, str]]):
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
                            kerning: int):
        x += kerning
        y += glyph.offset_y
        if canvas is not None:
            alpha = self._bitmaps[glyph.image_index].crop(
                (glyph.x, glyph.y, glyph.x + glyph.width, glyph.y + glyph.height))
            color = PIL.Image.new("L", alpha.size, 255)
            canvas.image.paste(
                color,
                (x, y),
                alpha
            )
        return x, y, x + glyph.width, y + glyph.height

    def __draw(self, canvas: typing.Optional[Canvas], x: int, y: int, text: str):
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
            return self.__draw_single_glyph(canvas, x, y, split[0], 0)

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
            now_l, now_t, now_r, now_b = self.__draw_single_glyph(canvas, cur_x, cur_y, glyph, kerning)

            left = min(left, now_l)
            right = max(right, now_r)

            cur_x = now_r + glyph.offset_x
            last_char = current_char

        return left, top, right, bottom

    def draw(self, canvas: Canvas, x: int, y: int, text: str):
        self.__draw(canvas, x, y, text)

    @property
    def size_pt(self) -> int:
        return int(round(self._fdt.fthd_header.size))

    @property
    @functools.cache
    def characters(self) -> typing.List[str]:
        return list(sorted(self._glyph_map.keys()))

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

    def draw(self, canvas: Canvas, x: int, y: int, text: str):
        canvas.draw.text((x, y), text, font=self._pilfont, fill=255, anchor="la")

    @property
    def size_pt(self) -> int:
        return self._size

    @property
    @functools.cache
    def characters(self) -> typing.List[str]:
        return list(sorted(self._character_map.keys()))

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


def create_fdt(font_mix: typing.Dict[str, typing.Tuple[typing.List[Font], typing.Optional[typing.Sequence[str]]]],
               canvases: typing.List[Canvas],
               image_width: int,
               image_height: int,
               kerning_treat_same_font_characters: str = " ",
               distance_between_glyphs: int = 0,
               maintain_full_height: bool = True
               ):
    # key: font_index, glyph_name
    # value: image_index, x, y
    first_rendered_glyphs: typing.Dict[typing.Tuple[Font, str], typing.Tuple[int, int, int]] = {}

    result: typing.Dict[str, bytes] = {}

    for new_font_index, (new_font_name, (font_list, use_chars)) in enumerate(font_mix.items()):
        if not isinstance(use_chars, set):
            use_chars = set() if use_chars is None else set(use_chars)
        print(f"[{new_font_index + 1}/{len(font_mix)}] {new_font_name}...")
        font_table_header = sqexdata.FdtFontTableHeader()
        font_table_header.signature = sqexdata.FdtFontTableHeader.SIGNATURE
        font_table_header.null_1 = 0
        font_table_header.image_width = image_width
        font_table_header.image_height = image_height
        font_table_header.size = max(x.size_pt for x in font_list)

        max_descent = max_ascent = 0
        for f in font_list:
            max_ascent = max(max_ascent, f.font_ascent)
            max_descent = max(max_descent, f.font_descent)
        font_table_header.font_height = max_descent + max_ascent
        font_table_header.font_ascent = max_ascent

        charmap: typing.Dict[str, int] = {}
        for i, f in enumerate(font_list):
            for text in f.characters:
                if text not in charmap and (use_chars is None or text in use_chars):
                    charmap[text] = i
        charmap = {x: charmap[x] for x in sorted(charmap.keys())}
        font_table_header.glyph_count = len(charmap)

        left_offset = 0
        max_pad_y = 0
        for text, font_index in charmap.items():
            font = font_list[font_index]
            actual_render_text = '.' if text == '\n' else text
            shift_x, *_ = font.getbbox(actual_render_text)
            left_offset = max(-shift_x, left_offset)
            max_pad_y = max(max_pad_y, max_ascent - font.font_ascent)

        # key: text
        glyphs: typing.Dict[str, sqexdata.FdtGlyphEntry] = {}

        for text_index, (text, font_index) in enumerate(charmap.items()):
            font = font_list[font_index]

            if text_index % 2048 == 0:
                print(
                    f"[{new_font_index + 1}/{len(font_mix)}] {new_font_name} [{text_index + 1}/{len(charmap)}] {text}...")

            glyph = sqexdata.FdtGlyphEntry()
            glyph.char = text

            actual_render_text = '.' if text == '\n' else text

            shift_x, shift_y, glyph.width, bottom = font.getbbox(actual_render_text)
            glyph.width = glyph.width - shift_x + left_offset
            glyph.offset_x = font.getcharwidth(text) - glyph.width

            if maintain_full_height:
                shift_y = 0
                pad_y = 0  # max_ascent - font.font_ascent
                glyph.height = font_table_header.font_height + pad_y
            else:
                glyph.height = bottom - shift_y
                glyph.offset_y = shift_y

            if rendered := first_rendered_glyphs.get((font, text), None):
                glyph.image_index, glyph.x, glyph.y = rendered
            else:
                new_canvas = False
                if not canvases:
                    new_canvas = True
                elif canvases[-1].x + glyph.width >= canvases[-1].image.width - distance_between_glyphs:
                    canvases[-1].x = distance_between_glyphs
                    canvases[-1].y += canvases[-1].line_height + distance_between_glyphs
                    canvases[-1].line_height = 0
                    if canvases[-1].y + glyph.height + distance_between_glyphs >= canvases[-1].image.height:
                        new_canvas = True

                if new_canvas:
                    canvases.append(Canvas(image_width, image_height))
                    canvases[-1].x = canvases[-1].y = distance_between_glyphs
                    print(f"Canvas #{len(canvases)}")

                glyph.image_index = len(canvases) - 1

                glyph.x, glyph.y = canvases[-1].x, canvases[-1].y
                font.draw(canvases[-1],
                          glyph.x + left_offset,
                          glyph.y - shift_y,
                          actual_render_text)

                canvases[-1].x += glyph.width + distance_between_glyphs
                canvases[-1].line_height = max(canvases[-1].line_height, glyph.height)

            glyphs[text] = glyph
            first_rendered_glyphs[font, text] = glyph.image_index, glyph.x, glyph.y

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
        for i, font in enumerate(font_list):
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
                    if c1 not in kerning_treat_same_font_characters and c2 not in kerning_treat_same_font_characters:
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

        result[new_font_name] = fp.getvalue()

    return result


def render_text2(font: Font, txt: str):
    l, t, r, b = font.getbbox(txt)
    w = r - l
    h = b - t

    font.draw(canvas := Canvas(w, h), -l, -t, txt)
    return canvas.image


def render_char(font: Font, char: str, on: PIL.Image.Image, on_x, on_y):
    l, t, r, b = font.getbbox(char)
    w = r - l
    h = b - t

    font.draw(canvas := Canvas(w, h), -l, -t, char)
    draw = PIL.ImageDraw.Draw(on)
    # draw.line((on_x + r + font.last_offset_x, on_y, on_x + r + font.last_offset_x, on_y + b), fill=(255, 0, 0), width=1)
    draw.rectangle((on_x + l, on_y + t, on_x + r, on_y + b), outline=(0, 0, 255))
    draw.rectangle((on_x, on_y, on_x + r, on_y + b), outline=(0, 255, 0))

    on.paste(
        PIL.Image.new("RGB", canvas.image.size, (255, 255, 255)),
        (on_x + l, on_y + t),
        canvas.image
    )
    return on_x + w


def transform_sqex(orig_tex_normal, orig_tex_lobby):
    comics = {}
    notos = {}
    new_font_def_lobby = {}
    new_font_def_normal = {}

    hangul_range = set(chr(x) for x in itertools.chain(
        range(0x1100, 0x1200),  # Hangul Jamo
        # range(0x3000, 0x3040),  # CJK Symbols and Punctuation
        range(0x3130, 0x3190),  # Hangul Compatibility Jamo
        # range(0x3200, 0x3300),  # Enclosed CJK Letters and Months
        # range(0x3400, 0x4DC0),  # CJK Unified Ideographs Extensions A
        # range(0x4E00, 0xA000),  # CJK Unified Ideographs
        # range(0xA960, 0xA97F),  # Hangul Jamo Extended-A
        range(0xAC00, 0xD7B0),  # Hangul Syllables
        # range(0xD7B0, 0xD800),  # Hangul Jamo Extended-B
        # range(0xF900, 0xFB00),  # CJK Compatibility Ideographs
        # range(0xFF00, 0xFFF0),  # Halfwidth and Fullwidth Forms
        # range(0x20000, 0x2A6E0),  # CJK Unified Ideographs Extensions B
        # range(0x2A700, 0x2B740),  # CJK Unified Ideographs Extensions C
        # range(0x2B740, 0x2B820),  # CJK Unified Ideographs Extensions D
        # range(0x2B820, 0x2CEB0),  # CJK Unified Ideographs Extensions E
        # range(0x2CEB0, 0x2EBF0),  # CJK Unified Ideographs Extensions F
        # range(0x2F800, 0x2FA20),  # CJK Compatibility Ideographs Supplement
    ))

    for fn in os.listdir(r"Z:\scratch\g\ffxiv\000000\common\font"):
        if fn.endswith(".fdt"):
            size = int(fn.split("_")[1][:2])
            if fn.endswith("_lobby.fdt"):
                texs = orig_tex_lobby
                defdict = new_font_def_lobby
            else:
                texs = orig_tex_normal
                defdict = new_font_def_normal

            if size == 96:
                size = 10  # 9.6 actually
            if size not in comics:
                comics[size] = FreeTypeFont(
                    r"C:\Windows\Fonts\comic.ttf",
                    # r"C:\Windows\Fonts\PAPYRUS.TTF",
                    # r"C:\Users\SP\AppData\Local\Microsoft\Windows\Fonts\ElliotSix.ttf",
                    0, size)
            base_font = SqexFont(rf"Z:\scratch\g\ffxiv\000000\common\font\{fn}", texs)
            d = [
                # comics[size],
                FreeTypeFont(r"C:\windows\fonts\gulim.ttc", 0, size),
                base_font,
            ]
            use_chars = set(base_font.characters)
            if 'axis' in fn.lower() and False:
                if size not in notos:
                    # notos[size] = FreeTypeFont(r"F:\Downloads\SourceHanSans\OTF\Korean\SourceHanSansK-Medium.otf", 0, size)
                    notos[size] = FreeTypeFont(r"C:\windows\fonts\gulim.ttc", 0, size)
                # d.append(notos[size])
                d.insert(len(d) - 1, notos[size])
                # use_chars.update(hangul_range)

            defdict[fn[:-4]] = (d, use_chars)

    return new_font_def_normal, new_font_def_lobby


def __main__():
    # dpa = r"C:\Program Files (x86)\SquareEnix\FINAL FANTASY XIV - A Realm Reborn\game\sqpack\ffxiv\000000.win32\common\font"
    # for p in os.listdir(dpa):
    #     if p[-4:] == '.fdt':
    #         with open(f"{dpa}\\{p}", "a+b") as fp:
    #             fp.seek(0, os.SEEK_SET)
    #             h1 = sqexdata.FdtHeader()
    #             fp.readinto(h1)
    #             h2 = sqexdata.FdtFontTableHeader()
    #             fp.readinto(h2)
    #             h2.left_offset = 0
    #             fp.seek(ctypes.sizeof(h1))
    #             fp.write(h2)
    # return 0
    orig_tex = [
        fr"Z:\scratch\g\ffxiv\000000\common\font\font{i}.tex"
        for i in range(1, 8)
    ]
    orig_tex_lobby = [
        fr"Z:\scratch\g\ffxiv\000000\common\font\font_lobby{i}.tex"
        for i in range(1, 3)
    ]
    canvas_w = canvas_h = 1024
    new_font_def_normal, new_font_def_lobby = transform_sqex(orig_tex, orig_tex_lobby)

    # constan = FreeTypeFont(r"C:\Windows\Fonts\constan.ttf", 0, 20)
    # gulim = FreeTypeFont(r"C:\Windows\Fonts\gulim.ttc", 0, 16)
    # times = FreeTypeFont(r"C:\Windows\Fonts\Times.ttf", 0, 20)
    # comic = FreeTypeFont(r"C:\Windows\Fonts\comic.ttf", 0, 18)
    # papy = FreeTypeFont(r"C:\Windows\Fonts\PAPYRUS.TTF", 0, 36)
    # ax36 = SqexFont(rf"Z:\scratch\g\ffxiv\000000\common\font\AXIS_36.fdt", orig_tex)
    # trndr = SqexFont(r"C:\Program Files (x86)\SquareEnix\FINAL FANTASY XIV - A Realm Reborn\game\sqpack\ffxiv\000000.win32\common\font\AXIS_36.fdt",
    #                  [x for x in [
    #     fr"C:\Program Files (x86)\SquareEnix\FINAL FANTASY XIV - A Realm Reborn\game\sqpack\ffxiv\000000.win32\common\font\font_lobby{i}.tex"
    #     for i in range(1, 8)
    # ] if os.path.exists(x)])
    # img = PIL.Image.new("RGB", (1024, 1024))
    # nx = 10
    # for i, c in enumerate("Sometimes I question"):
    #     nx = render_char(trndr, c, img, nx, 10) + 5
    # exit(img.show())
    # exit(render_text2(ax36, "Sometimes I question").show())
    # canvas_w = canvas_h = 1024
    # new_font_def = {
    #     "Papyrus36": ([papy, ax36], None),
    #     "AxisRecreation": ([axis], None),
    #     "Constantia": ([constan], None),
    #     "Times": ([times], None),
    #     "Gulim": ([gulim], None),
    #     "ComicGulim": ([
    #         comic,
    #         gulim,
    #     ], None),
    #     "ComicGulimAxis": ([
    #         comic,
    #         gulim,
    #         axis,
    #     ], None),
    #     "TimesGulim": ([
    #         times,
    #         gulim,
    #     ], None),
    #     "TimesGulimAxis": ([
    #         times,
    #         gulim,
    #         axis,
    #     ], None),
    #     "ConstantiaExtra": ([
    #         constan,
    #         comic,
    #         gulim,
    #         axis
    #     ], None),
    # }

    if True:
        shutil.rmtree("t/", ignore_errors=True)
        os.makedirs("t", exist_ok=True)
        for is_lobby, new_font_def in enumerate((new_font_def_normal, new_font_def_lobby)):
            canvases = []
            fdts = create_fdt(new_font_def, canvases, canvas_w, canvas_h)

            for font_name, font_bytes in fdts.items():
                with open(f"t/{font_name}.fdt", "wb") as fp:
                    fp.write(font_bytes)
            for _ in range(len(canvases), (len(canvases) + 3) // 4 * 4):
                canvases.append(Canvas(canvas_w, canvas_h))

            tex_header = sqexdata.TexHeader()
            tex_header.header_size = 0x80
            tex_header.compression_type = 0x1440
            tex_header.decompressed_width = canvas_w
            tex_header.decompressed_height = canvas_h
            tex_header.depth = 1
            tex_header.num_mipmaps = 1

            for i in range(0, len(canvases), 4):
                b, g, r, a = tuple(x.image for x in canvases[i:i + 4])
                img: PIL.Image.Image = PIL.Image.merge("RGBA", (r, g, b, a))
                img_bytes = sqexdata.ImageEncoding.rgba4444(img)
                # img_bytes = sqexdata.ImageEncoding.bgra(img)
                with open(f"t/font{'_lobby' if is_lobby else ''}{1 + (i // 4)}.tex", "wb") as fp:
                    fp: typing.Union[typing.BinaryIO, io.RawIOBase]
                    fp.write(tex_header)
                    fp.write(b"\x50")
                    fp.seek(0x50, os.SEEK_SET)
                    fp.write(img_bytes)

            for i, c in enumerate(canvases):
                c.image.save(f"t/z_1_{is_lobby}_{i}.png")

    tex_paths_normal = [
        fr"t\font{i}.tex"
        for i in range(1, 8)
    ]
    tex_paths_lobby = [
        fr"t\font_lobby{i}.tex"
        for i in range(1, 3)
    ]

    for is_lobby, (new_font_def, tex_paths) in enumerate((
            (new_font_def_normal, tex_paths_normal),
            (new_font_def_lobby, tex_paths_lobby))
    ):
        for fontname in new_font_def.keys():
            new_font = SqexFont(f"t/{fontname}.fdt", [x for x in tex_paths if os.path.exists(x)])
            rtext = f"t/{fontname}.fdt\n\n{SHOWCASE_TEXT}"
            # rtext = "ojajejijoju"
            render_text2(new_font, rtext).save(f"t/z_2_{is_lobby}_{fontname}.png")
    return 0


if __name__ == "__main__":
    exit(__main__())
