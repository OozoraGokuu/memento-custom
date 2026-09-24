#pragma once
class Context;
class QQmlApplicationEngine;
// Invoked only by --smoke-test, with an isolated application profile.
void runReleaseCheck(Context &context, QQmlApplicationEngine &engine);
