#pragma once

/* Minimal test harness. Qt Test is not part of the OBS dependency bundle, so
 * the accessibility tests register themselves here instead. */

#include <functional>
#include <string>
#include <vector>

namespace A11yTest {

struct TestCase {
	const char *name;
	std::function<void()> body;
};

std::vector<TestCase> &Registry();
void ReportFailure(const char *file, int line, const std::string &message);

struct Registrar {
	Registrar(const char *name, std::function<void()> body) { Registry().push_back({name, std::move(body)}); }
};

} // namespace A11yTest

#define A11Y_TEST(name)                                                  \
	static void name();                                              \
	static A11yTest::Registrar name##_registrar(#name, name); \
	static void name()

#define A11Y_CHECK(condition)                                                    \
	do {                                                                     \
		if (!(condition)) {                                              \
			A11yTest::ReportFailure(__FILE__, __LINE__, #condition); \
		}                                                                \
	} while (false)

#define A11Y_CHECK_MESSAGE(condition, message)                                   \
	do {                                                                     \
		if (!(condition)) {                                              \
			A11yTest::ReportFailure(__FILE__, __LINE__, message);    \
		}                                                                \
	} while (false)
