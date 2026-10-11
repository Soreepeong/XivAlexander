#pragma once

#include "Base.h"

// jsmn's functions are compiled into Json.cpp only.
#define JSMN_STRICT
#ifndef XA_JSON_IMPLEMENTATION
#define JSMN_HEADER
#endif
#include <jsmn.h>

// The few settings the loader needs, read from the tokens jsmn makes of a JSON document. Only a document that is one
// value (and whitespace) is taken; a document that does not parse yields nothing, as with nlohmann::json::parse.
namespace Loader::Json {
	class Document {
		const char* m_text = nullptr;
		jsmntok_t* m_tokens = nullptr;
		int m_count = 0;

		friend struct Value;

	public:
		Document() = default;
		Document(const Document&) = delete;
		Document& operator=(const Document&) = delete;
		~Document() { Free(m_tokens); }

		[[nodiscard]] bool Parse(const char* data, size_t size);
		[[nodiscard]] struct Value Root() const;
	};

	struct Value {
		const Document* Doc = nullptr;
		int Index = -1;

		[[nodiscard]] bool Exists() const { return Doc && Index >= 0; }
		[[nodiscard]] bool Is(jsmntype_t type) const { return Exists() && Doc->m_tokens[Index].type == type; }
		[[nodiscard]] bool IsObject() const { return Is(JSMN_OBJECT); }
		[[nodiscard]] bool IsArray() const { return Is(JSMN_ARRAY); }
		[[nodiscard]] bool IsString() const { return Is(JSMN_STRING); }

		// The index of the token after this value and everything in it.
		[[nodiscard]] int Next() const;

		// For a string, the length of its text between the quotes, which no decoded form exceeds.
		[[nodiscard]] size_t RawLength() const;

		// Decodes a string (escapes resolved, \uXXXX pairs combined) to UTF-8 into out, which holds RawLength bytes.
		// False if the escapes are invalid.
		bool DecodeUtf8(char* out, size_t& length) const;
		[[nodiscard]] Str DecodeWide() const;
		[[nodiscard]] bool Equals(const char* utf8) const;
	};

	// Calls f(key, value) for each member of an object, in document order.
	void ForEachMember(const Value& obj, void(*f)(void* ctx, const Value& key, const Value& value), void* ctx);
	void ForEachElement(const Value& arr, void(*f)(void* ctx, const Value& value), void* ctx);

	// The value of the last member named key, as nlohmann::json keeps the last of duplicate keys.
	[[nodiscard]] Value Find(const Value& obj, const char* key);
}
