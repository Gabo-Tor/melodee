// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kerem Kilic (Ellic Studio)
// Legacy controller tests mock the new session boundary. Its real dialogs,
// locks and recovery state are exercised in test_install_safety.mjs.
export function mockInstallSession(context) {
  context.SteadyUpdater = context.Updater;
  context.createInstallSession = () => ({begin:async () => true,step(){},message(){},finish(){},resume(){},probe:fn=>fn(),busy:async () => false});
}
