#include "pch.h"
#include "include\CallOnDestruction.h"

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

Utils::CallOnDestruction::~CallOnDestruction() {
	if (m_fn)
		m_fn();
}

Utils::CallOnDestruction::operator bool() const {
	return !!m_fn;
}
