#include "pch.h"
#include "Utils_CallOnDestruction.h"

/// Constructors that will not call anything on destruction
Utils::CallOnDestruction::CallOnDestruction() noexcept = default;
Utils::CallOnDestruction::CallOnDestruction(std::nullptr_t) noexcept {}

/// Constructor that will call something on destruction
Utils::CallOnDestruction::CallOnDestruction(std::function<void()> fn) :
	m_fn(std::move(fn)) {
}

/// Constructor that will move the destruction callback from r to this
Utils::CallOnDestruction::CallOnDestruction(CallOnDestruction && r) noexcept {
	m_fn = std::move(r.m_fn);
	r.m_fn = nullptr;
}

/// Movement operator
Utils::CallOnDestruction& Utils::CallOnDestruction::operator=(CallOnDestruction && r) noexcept {
	if (m_fn)
		m_fn();
	m_fn = std::move(r.m_fn);
	r.m_fn = nullptr;
	return *this;
}

/// Null assignment operator
Utils::CallOnDestruction& Utils::CallOnDestruction::operator=(std::nullptr_t) noexcept {
	if (!m_fn)
		return *this;
	m_fn();
	m_fn = nullptr;
	return *this;
}

Utils::CallOnDestruction& Utils::CallOnDestruction::operator=(std::function<void()>&& fn) noexcept {
	if (m_fn)
		m_fn();
	m_fn = std::move(fn);
	return *this;
}

Utils::CallOnDestruction& Utils::CallOnDestruction::operator=(const std::function<void()>& fn) {
	if (m_fn)
		m_fn();
	m_fn = fn;
	return *this;
}

Utils::CallOnDestruction& Utils::CallOnDestruction::Wrap(std::function<void(std::function<void()>)> wrapper) {
	m_fn = [fn = std::move(m_fn), wrapper = std::move(wrapper)]() {
		wrapper(fn);
	};
	return *this;
}

Utils::CallOnDestruction::~CallOnDestruction() {
	if (m_fn)
		m_fn();
}

Utils::CallOnDestruction::operator bool() const {
	return !!m_fn;
}

Utils::CallOnDestruction::Multiple::Multiple() = default;

Utils::CallOnDestruction::Multiple& Utils::CallOnDestruction::Multiple::operator+=(CallOnDestruction o) {
	if (o)
		m_list.emplace_back(std::move(o));
	return *this;
}

Utils::CallOnDestruction::Multiple& Utils::CallOnDestruction::Multiple::operator+=(std::function<void()> f) {
	m_list.emplace_back(f);
	return *this;
}

Utils::CallOnDestruction::Multiple& Utils::CallOnDestruction::Multiple::operator+=(Multiple r) {
	m_list.insert(m_list.end(), std::make_move_iterator(r.m_list.begin()), std::make_move_iterator(r.m_list.end()));
	r.m_list.clear();
	return *this;
}

void Utils::CallOnDestruction::Multiple::Clear() {
	while (!m_list.empty())
		m_list.pop_back();
}

Utils::CallOnDestruction::Multiple::~Multiple() {
	Clear();
}
