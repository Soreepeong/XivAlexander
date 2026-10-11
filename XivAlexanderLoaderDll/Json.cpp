#define XA_JSON_IMPLEMENTATION
#include "Json.h"

namespace {
	bool IsWhitespace(char c) {
		return c == ' ' || c == '\t' || c == '\n' || c == '\r';
	}

	int HexDigit(char c) {
		if (c >= '0' && c <= '9')
			return c - '0';
		if (c >= 'a' && c <= 'f')
			return c - 'a' + 10;
		if (c >= 'A' && c <= 'F')
			return c - 'A' + 10;
		return -1;
	}

	bool ReadHex4(const char* p, const char* e, uint32_t& value) {
		if (e - p < 4)
			return false;
		value = 0;
		for (int i = 0; i < 4; ++i) {
			const auto d = HexDigit(p[i]);
			if (d < 0)
				return false;
			value = value << 4 | static_cast<uint32_t>(d);
		}
		return true;
	}
}

bool Loader::Json::Document::Parse(const char* data, size_t size) {
	Free(m_tokens);
	m_tokens = nullptr;
	m_count = 0;
	m_text = data;

	jsmn_parser parser;
	jsmn_init(&parser);
	const auto count = jsmn_parse(&parser, data, size, nullptr, 0);
	if (count <= 0)
		return false;
	m_tokens = static_cast<jsmntok_t*>(Alloc(static_cast<size_t>(count) * sizeof(jsmntok_t)));
	jsmn_init(&parser);
	if (jsmn_parse(&parser, data, size, m_tokens, static_cast<unsigned>(count)) != count)
		return false;

	// One value, with nothing but whitespace (and a UTF-8 byte order mark) around it.
	auto first = 0;
	if (size >= 3 && static_cast<unsigned char>(data[0]) == 0xEF && static_cast<unsigned char>(data[1]) == 0xBB && static_cast<unsigned char>(data[2]) == 0xBF)
		first = 3;
	m_count = count;
	const auto root = Root();
	auto start = m_tokens[0].start;
	auto end = m_tokens[0].end;
	// jsmn's string tokens leave out the quotes.
	if (m_tokens[0].type == JSMN_STRING) {
		--start;
		++end;
	}
	for (auto i = first; i < start; ++i) {
		if (!IsWhitespace(data[i]))
			return m_count = 0, false;
	}
	for (auto i = static_cast<size_t>(end); i < size; ++i) {
		if (!IsWhitespace(data[i]))
			return m_count = 0, false;
	}
	if (root.Next() != count)
		return m_count = 0, false;
	return true;
}

Loader::Json::Value Loader::Json::Document::Root() const {
	return m_count ? Value{this, 0} : Value{};
}

int Loader::Json::Value::Next() const {
	const auto& tokens = Doc->m_tokens;
	auto i = Index + 1;
	while (i < Doc->m_count && tokens[i].start < tokens[Index].end)
		++i;
	return i;
}

size_t Loader::Json::Value::RawLength() const {
	const auto& t = Doc->m_tokens[Index];
	return static_cast<size_t>(t.end - t.start);
}

bool Loader::Json::Value::DecodeUtf8(char* out, size_t& length) const {
	length = 0;
	if (!IsString())
		return false;
	const auto& t = Doc->m_tokens[Index];
	auto p = Doc->m_text + t.start;
	const auto e = Doc->m_text + t.end;
	size_t n = 0;
	const auto put = [&](uint32_t c) {
		if (c < 0x80) {
			out[n++] = static_cast<char>(c);
		} else if (c < 0x800) {
			out[n++] = static_cast<char>(0xC0 | c >> 6);
			out[n++] = static_cast<char>(0x80 | (c & 0x3F));
		} else if (c < 0x10000) {
			out[n++] = static_cast<char>(0xE0 | c >> 12);
			out[n++] = static_cast<char>(0x80 | (c >> 6 & 0x3F));
			out[n++] = static_cast<char>(0x80 | (c & 0x3F));
		} else {
			out[n++] = static_cast<char>(0xF0 | c >> 18);
			out[n++] = static_cast<char>(0x80 | (c >> 12 & 0x3F));
			out[n++] = static_cast<char>(0x80 | (c >> 6 & 0x3F));
			out[n++] = static_cast<char>(0x80 | (c & 0x3F));
		}
	};
	while (p < e) {
		if (*p != '\\') {
			out[n++] = *p++;
			continue;
		}
		if (++p >= e)
			return false;
		switch (*p++) {
			case '"': put('"'); break;
			case '\\': put('\\'); break;
			case '/': put('/'); break;
			case 'b': put('\b'); break;
			case 'f': put('\f'); break;
			case 'n': put('\n'); break;
			case 'r': put('\r'); break;
			case 't': put('\t'); break;
			case 'u': {
				uint32_t u;
				if (!ReadHex4(p, e, u))
					return false;
				p += 4;
				if (u >= 0xD800 && u <= 0xDBFF) {
					uint32_t low;
					if (e - p < 6 || p[0] != '\\' || p[1] != 'u' || !ReadHex4(p + 2, e, low) || low < 0xDC00 || low > 0xDFFF)
						return false;
					p += 6;
					u = 0x10000 + ((u - 0xD800) << 10) + (low - 0xDC00);
				} else if (u >= 0xDC00 && u <= 0xDFFF) {
					return false;
				}
				put(u);
				break;
			}
			default:
				return false;
		}
	}
	length = n;
	return true;
}

Loader::Str Loader::Json::Value::DecodeWide() const {
	if (!IsString())
		return {};
	const auto buf = static_cast<char*>(Alloc(RawLength()));
	size_t n;
	auto res = DecodeUtf8(buf, n) ? FromUtf8(buf, n) : Str();
	Free(buf);
	return res;
}

bool Loader::Json::Value::Equals(const char* utf8) const {
	if (!IsString())
		return false;
	const auto buf = static_cast<char*>(Alloc(RawLength()));
	size_t n;
	const auto result = DecodeUtf8(buf, n) && n == Length(utf8) && memcmp(buf, utf8, n) == 0;
	Free(buf);
	return result;
}

void Loader::Json::ForEachMember(const Value& obj, void(*f)(void* ctx, const Value& key, const Value& value), void* ctx) {
	if (!obj.IsObject())
		return;
	const auto end = obj.Next();
	for (auto i = obj.Index + 1; i < end;) {
		const auto key = Value{obj.Doc, i};
		const auto value = Value{obj.Doc, i + 1};
		if (value.Index >= end)
			return;
		f(ctx, key, value);
		i = value.Next();
	}
}

void Loader::Json::ForEachElement(const Value& arr, void(*f)(void* ctx, const Value& value), void* ctx) {
	if (!arr.IsArray())
		return;
	const auto end = arr.Next();
	for (auto i = arr.Index + 1; i < end;) {
		const auto value = Value{arr.Doc, i};
		f(ctx, value);
		i = value.Next();
	}
}

Loader::Json::Value Loader::Json::Find(const Value& obj, const char* key) {
	struct Context {
		const char* Key;
		Value Found;
	} ctx{key, {}};
	ForEachMember(obj, [](void* p, const Value& k, const Value& v) {
		auto& c = *static_cast<Context*>(p);
		if (k.Equals(c.Key))
			c.Found = v;
	}, &ctx);
	return ctx.Found;
}
