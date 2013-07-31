/****************************************************************************
**
** Copyright (C) 2013 Digia Plc and/or its subsidiary(-ies).
** Contact: http://www.qt-project.org/legal
**
** This file is part of Qt Creator.
**
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and Digia.  For licensing terms and
** conditions see http://qt.digia.com/licensing.  For further information
** use the contact form at http://qt.digia.com/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU Lesser General Public License version 2.1 requirements
** will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Digia gives you certain additional
** rights.  These rights are described in the Digia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
****************************************************************************/

#include "qmlprofilerruncontrolfactory.h"
#include "localqmlprofilerrunner.h"
#include "qmlprofilerengine.h"

#include <analyzerbase/ianalyzertool.h>
#include <analyzerbase/analyzermanager.h>
#include <analyzerbase/analyzerstartparameters.h>
#include <analyzerbase/analyzerruncontrol.h>
#include <analyzerbase/analyzersettings.h>

#include <debugger/debuggerrunconfigurationaspect.h>

#include <projectexplorer/environmentaspect.h>
#include <projectexplorer/kitinformation.h>
#include <projectexplorer/localapplicationrunconfiguration.h>
#include <projectexplorer/projectexplorer.h>
#include <projectexplorer/project.h>
#include <projectexplorer/session.h>
#include <projectexplorer/target.h>

#include <qmlprojectmanager/qmlprojectrunconfiguration.h>

#include <utils/qtcassert.h>

#include <QTcpServer>

using namespace Analyzer;
using namespace ProjectExplorer;

namespace QmlProfiler {
namespace Internal {

QmlProfilerRunControlFactory::QmlProfilerRunControlFactory(QObject *parent) :
    IRunControlFactory(parent)
{
}

bool QmlProfilerRunControlFactory::canRun(RunConfiguration *runConfiguration, RunMode mode) const
{
    return mode == QmlProfilerRunMode
            && (qobject_cast<QmlProjectManager::QmlProjectRunConfiguration *>(runConfiguration)
                || qobject_cast<LocalApplicationRunConfiguration *>(runConfiguration));
}

static AnalyzerStartParameters createQmlProfilerStartParameters(RunConfiguration *runConfiguration)
{
    AnalyzerStartParameters sp;
    EnvironmentAspect *environment = runConfiguration->extraAspect<EnvironmentAspect>();
    Debugger::DebuggerRunConfigurationAspect *debugger
            = runConfiguration->extraAspect<Debugger::DebuggerRunConfigurationAspect>();
    QTC_ASSERT(debugger, return sp);

    // FIXME: This is only used to communicate the connParams settings.
    if (QmlProjectManager::QmlProjectRunConfiguration *rc1 =
            qobject_cast<QmlProjectManager::QmlProjectRunConfiguration *>(runConfiguration)) {
        // This is a "plain" .qmlproject.
        if (environment)
            sp.environment = environment->environment();
        sp.workingDirectory = rc1->workingDirectory();
        sp.debuggee = rc1->observerPath();
        sp.debuggeeArgs = rc1->viewerArguments();
        sp.displayName = rc1->displayName();
    } else if (LocalApplicationRunConfiguration *rc2 =
            qobject_cast<LocalApplicationRunConfiguration *>(runConfiguration)) {
        if (environment)
            sp.environment = environment->environment();
        sp.workingDirectory = rc2->workingDirectory();
        sp.debuggee = rc2->executable();
        sp.debuggeeArgs = rc2->commandLineArguments();
        sp.displayName = rc2->displayName();
    } else {
        // What could that be?
        QTC_ASSERT(false, return sp);
    }
    const IDevice::ConstPtr device = DeviceKitInformation::device(runConfiguration->target()->kit());
    if (device->type() == ProjectExplorer::Constants::DESKTOP_DEVICE_TYPE) {
        QTcpServer server;
        if (!server.listen(QHostAddress::LocalHost) && !server.listen(QHostAddress::LocalHostIPv6)) {
            qWarning() << "Cannot open port on host for QML profiling.";
            return sp;
        }
        sp.analyzerHost = server.serverAddress().toString();
        sp.analyzerPort = server.serverPort();
    }
    sp.startMode = StartQml;
    return sp;
}

RunControl *QmlProfilerRunControlFactory::create(RunConfiguration *runConfiguration, RunMode mode, QString *errorMessage)
{
    IAnalyzerTool *tool = AnalyzerManager::toolFromRunMode(mode);
    if (!tool) {
        if (errorMessage)
            *errorMessage = tr("No analyzer tool selected"); // never happens
        return 0;
    }

    QTC_ASSERT(canRun(runConfiguration, mode), return 0);

    AnalyzerStartParameters sp = createQmlProfilerStartParameters(runConfiguration);
    sp.runMode = mode;

    // only desktop device is supported
    const IDevice::ConstPtr device = DeviceKitInformation::device(runConfiguration->target()->kit());
    QTC_ASSERT(device->type() == ProjectExplorer::Constants::DESKTOP_DEVICE_TYPE, return 0);

    AnalyzerRunControl *rc = tool->createRunControl(sp, runConfiguration);
    QmlProfilerRunControl *engine = qobject_cast<QmlProfilerRunControl *>(rc);
    if (!engine) {
        delete rc;
        return 0;
    }
    LocalQmlProfilerRunner *runner = LocalQmlProfilerRunner::createLocalRunner(runConfiguration, sp, errorMessage, engine);
    if (!runner)
        return 0;
    connect(runner, SIGNAL(stopped()), engine, SLOT(notifyRemoteFinished()));
    connect(runner, SIGNAL(appendMessage(QString,Utils::OutputFormat)),
            engine, SLOT(logApplicationMessage(QString,Utils::OutputFormat)));
    connect(engine, SIGNAL(starting(const Analyzer::AnalyzerRunControl*)), runner,
            SLOT(start()));
    connect(rc, SIGNAL(finished()), runner, SLOT(stop()));
    return rc;
}

} // namespace Internal
} // namespace QmlProfiler
