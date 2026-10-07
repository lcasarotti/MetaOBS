#include "a11y-test.hpp"

#include <QApplication>

#include <cstring>
#include <iostream>

namespace A11yTest {

namespace {
int currentFailures = 0;
}

std::vector<TestCase> &Registry()
{
	static std::vector<TestCase> registry;
	return registry;
}

void ReportFailure(const char *file, int line, const std::string &message)
{
	currentFailures++;
	std::cout << "    " << file << ":" << line << ": " << message << "\n";
}

} // namespace A11yTest

/* Usage: obs-a11y-tests [prefix]. Runs every test whose name starts with the
 * prefix, or all tests when no prefix is given. */
int main(int argc, char *argv[])
{
	if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
		qputenv("QT_QPA_PLATFORM", "minimal");
	}

	QApplication app(argc, argv);

	const char *prefix = argc > 1 ? argv[1] : nullptr;
	int failedTests = 0;
	int ranTests = 0;

	for (const A11yTest::TestCase &test : A11yTest::Registry()) {
		if (prefix && std::strncmp(test.name, prefix, std::strlen(prefix)) != 0) {
			continue;
		}

		A11yTest::currentFailures = 0;
		test.body();
		ranTests++;

		if (A11yTest::currentFailures > 0) {
			failedTests++;
			std::cout << "FAIL " << test.name << "\n";
		} else {
			std::cout << "PASS " << test.name << "\n";
		}
	}

	std::cout << ranTests - failedTests << " of " << ranTests << " tests passed\n";
	return failedTests == 0 && ranTests > 0 ? 0 : 1;
}
