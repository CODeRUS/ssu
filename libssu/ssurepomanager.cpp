/**
 * @file ssurepomanager.cpp
 * @copyright 2013 - 2019 Jolla Ltd.
 * @copyright 2019 Open Mobile Platform Ltd.
 * @copyright LGPLv2+
 * @date 2013 - 2019
 */

/*
 *  This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <QStringList>
#include <QRegExp>
#include <QDirIterator>

#include <zypp/RepoManager.h>
#include <zypp/RepoInfo.h>

#include "sandbox_p.h"
#include "ssudeviceinfo.h"
#include "ssurepomanager.h"
#include "ssucoreconfig_p.h"
#include "ssusettings_p.h"
#include "ssulog_p.h"
#include "ssuvariables_p.h"
#include "ssufeaturemanager.h"
#include "ssu.h"

#include "../constants.h"

SsuRepoManager::SsuRepoManager()
    : QObject()
{
}

int SsuRepoManager::add(const QString &repo, const QString &repoUrl)
{
    SsuCoreConfig *ssuSettings = SsuCoreConfig::instance();

    // adding a repo is a noop when device is in update mode...
    if ((ssuSettings->deviceMode() & Ssu::UpdateMode) == Ssu::UpdateMode)
        return -1;

    // ... or in AppInstallMode
    if ((ssuSettings->deviceMode() & Ssu::AppInstallMode) == Ssu::AppInstallMode)
        return -1;

    if (repoUrl.isEmpty()) {
        // just enable a repository which has URL in repos.ini
        QStringList enabledRepos;
        if (ssuSettings->contains("enabled-repos"))
            enabledRepos = ssuSettings->value("enabled-repos").toStringList();

        enabledRepos.append(repo);
        enabledRepos.removeDuplicates();
        ssuSettings->setValue("enabled-repos", enabledRepos);
    } else {
        ssuSettings->setValue("repository-urls/" + repo, repoUrl);
    }

    ssuSettings->sync();
    return 0;
}

QString SsuRepoManager::caCertificatePath(const QString &domain)
{
    SsuCoreConfig *settings = SsuCoreConfig::instance();
    SsuSettings repoSettings(SSU_REPO_CONFIGURATION, SSU_REPO_CONFIGURATION_DIR);

    QString ca = SsuVariables::variable(&repoSettings,
                                        (domain.isEmpty() ? settings->domain() : domain) + "-domain",
                                        "_ca-certificate").toString();
    if (!ca.isEmpty())
        return ca;

    // compat setting, can go away after some time
    if (settings->contains("ca-certificate"))
        return settings->value("ca-certificate").toString();

    return QString();
}

int SsuRepoManager::disable(const QString &repo)
{
    SsuCoreConfig *ssuSettings = SsuCoreConfig::instance();
    QStringList disabledRepos;

    if (ssuSettings->contains("disabled-repos"))
        disabledRepos = ssuSettings->value("disabled-repos").toStringList();

    disabledRepos.append(repo);
    disabledRepos.removeDuplicates();

    ssuSettings->setValue("disabled-repos", disabledRepos);
    ssuSettings->sync();

    return 0;
}

int SsuRepoManager::enable(const QString &repo)
{
    SsuCoreConfig *ssuSettings = SsuCoreConfig::instance();
    QStringList disabledRepos;

    if (ssuSettings->contains("disabled-repos")) {
        disabledRepos = ssuSettings->value("disabled-repos").toStringList();

        disabledRepos.removeAll(repo);
        disabledRepos.removeDuplicates();

        if (disabledRepos.size() > 0)
            ssuSettings->setValue("disabled-repos", disabledRepos);
        else
            ssuSettings->remove("disabled-repos");

        ssuSettings->sync();
    }


    return 0;
}

int SsuRepoManager::remove(const QString &repo)
{
    SsuCoreConfig *ssuSettings = SsuCoreConfig::instance();

    // removing a repo is a noop when device is in update mode...
    if ((ssuSettings->deviceMode() & Ssu::UpdateMode) == Ssu::UpdateMode)
        return -1;

    // ... or AppInstallMode
    if ((ssuSettings->deviceMode() & Ssu::AppInstallMode) == Ssu::AppInstallMode)
        return -1;

    if (ssuSettings->contains("repository-urls/" + repo))
        ssuSettings->remove("repository-urls/" + repo);

    QStringList sections;
    sections << "enabled-repos" << "disabled-repos";
    for (const QString &section: sections) {
        if (ssuSettings->contains(section)) {
            QStringList repos = ssuSettings->value(section).toStringList();
            repos.removeAll(repo);
            repos.removeDuplicates();
            if (repos.size() > 0)
                ssuSettings->setValue(section, repos);
            else
                ssuSettings->remove(section);
        }
    }
    ssuSettings->sync();

    return 0;
}

QStringList SsuRepoManager::repos(int filter)
{
    SsuDeviceInfo deviceInfo;
    SsuCoreConfig *ssuSettings = SsuCoreConfig::instance();
    bool rnd = false;

    if ((ssuSettings->deviceMode() & Ssu::RndMode) == Ssu::RndMode)
        rnd = true;

    return repos(rnd, deviceInfo, filter);
}

QStringList SsuRepoManager::repos(bool rnd, int filter)
{
    SsuDeviceInfo deviceInfo;

    return repos(rnd, deviceInfo, filter);
}

// @todo the non-device specific repository resolving should move from deviceInfo to repomanager
QStringList SsuRepoManager::repos(bool rnd, SsuDeviceInfo &deviceInfo, int filter)
{
    QStringList result;

    // read the adaptation specific repositories, as well as the default
    // repositories; default repos are read through deviceInfo as an
    // adaptation is allowed to disable core repositories
    result = deviceInfo.repos(rnd, filter);

    // read the repositories of the available features. While devices have
    // a default list of features to be installed those are only relevant
    // for bootstrapping, so this code just operates on installed features
    SsuFeatureManager featureManager;
    result.append(featureManager.repos(rnd, filter));

    // read user-defined repositories from ssu.ini. This step needs to
    // happen at the end, after all other required repositories are
    // added already

    // TODO: in strict mode, filter the repository list from there
    SsuCoreConfig *ssuSettings = SsuCoreConfig::instance();

    bool updateMode = false;
    bool appInstallMode = false;

    if ((ssuSettings->deviceMode() & Ssu::UpdateMode) == Ssu::UpdateMode)
        updateMode = true;

    if ((ssuSettings->deviceMode() & Ssu::AppInstallMode) == Ssu::AppInstallMode) {
        updateMode = true;
        appInstallMode = true;
    }

    if (filter == Ssu::NoFilter || filter == Ssu::UserFilter) {
        // user defined repositories, or ones overriding URLs for default ones
        // -> in update mode we need to check for each of those if it already
        //    exists. If it exists, keep it, if it does not, disable it
        ssuSettings->beginGroup("repository-urls");
        QStringList repoUrls = ssuSettings->allKeys();
        ssuSettings->endGroup();

        if (updateMode) {
            foreach (const QString &key, repoUrls) {
                if (result.contains(key))
                    result.append(key);
            }
        } else {
            result.append(repoUrls);
        }

        // read user-enabled repositories from ssu.ini
        if (ssuSettings->contains("enabled-repos") && !updateMode)
            result.append(ssuSettings->value("enabled-repos").toStringList());

        // if the store repository is enabled keep it enabled in AppInstallMode
        if (ssuSettings->contains("enabled-repos") && appInstallMode) {
            // TODO store should not be hardcoded, but come via some store plugin
            if (ssuSettings->value("enabled-repos").toStringList().contains("store"))
                result.append("store");
        }
    }

    if (filter == Ssu::NoFilter ||
            filter == Ssu::UserFilter ||
            filter == Ssu::BoardFilterUserBlacklist) {
        // read the disabled repositories from user configuration
        if (ssuSettings->contains("disabled-repos") && !updateMode) {
            foreach (const QString &key, ssuSettings->value("disabled-repos").toStringList())
                result.removeAll(key);
        }
    }


    result.sort();
    result.removeDuplicates();

    return result;
}

void SsuRepoManager::update()
{
    // - delete all non-ssu managed repositories (missing ssu_ prefix)
    // - create list of ssu-repositories for current adaptation
    // - go through ssu_* repositories, delete all which are not in the list; write others
    SsuCoreConfig *ssuSettings = SsuCoreConfig::instance();
    int deviceMode = ssuSettings->deviceMode();

    SsuLog *ssuLog = SsuLog::instance();

    if ((deviceMode & Ssu::DisableRepoManager) == Ssu::DisableRepoManager) {
        ssuLog->print(LOG_INFO, "Repo management requested, but not enabled (option 'deviceMode')");
        return;
    }

    // if device is misconfigured, always assume release mode
    bool rndMode = false;

    if ((deviceMode & Ssu::RndMode) == Ssu::RndMode)
        rndMode = true;

    // get list of device-specific repositories...
    QStringList repositoryList = repos(rndMode);

    zypp::RepoManager zyppManager;
    std::list<zypp::RepoInfo> zyppRepos = zyppManager.knownRepositories();

    // strict mode enabled -> delete all repositories not prefixed by ssu
    // assume configuration error if there are no device repos, and don't delete
    // anything, even in strict mode
    if ((deviceMode & Ssu::LenientMode) != Ssu::LenientMode && !repositoryList.isEmpty()) {
        foreach (zypp::RepoInfo zyppRepo, zyppRepos) {
            if (zyppRepo.filepath().basename().substr(0, 4) != "ssu_") {
                ssuLog->print(LOG_INFO, "Strict mode enabled, removing unmanaged repository " + QString::fromStdString(zyppRepo.name()));
                zyppManager.removeRepository(zyppRepo);
            }
        }
    }

    // ... delete all ssu-managed repositories not valid for this device ...
    zyppRepos = zyppManager.knownRepositories();
    foreach (zypp::RepoInfo zyppRepo, zyppRepos) {
        if (zyppRepo.filepath().basename().substr(0, 4) == "ssu_") {
            QStringList parts = QString::fromStdString(zyppRepo.filepath().basename()).split("_");
            // repo file structure is ssu_<reponame>_<rnd|release>.repo -> splits to 3 parts
            if (parts.count() == 3) {
                if (!repositoryList.contains(parts.at(1)) ||
                        parts.at(2) != (rndMode ? "rnd.repo" : "release.repo" )) {
                    // This will also remove metadata and cached packages from this repo
                    zyppManager.removeRepository(zyppRepo);
                }
            } else {
                // This will also remove metadata and cached packages from this repo
                zyppManager.removeRepository(zyppRepo);
            }
        }
    }

    // ... and create all repositories required for this device
    foreach (const QString &repo, repositoryList) {
        // repo should be used where a unique identifier for silly human brains, or
        // zypper is required. repoName contains the shortened form for ssu use
        QString repoName = repo;
        QString debugSplit;
        if (repo.endsWith("-debuginfo")) {
            debugSplit = "&debug";
            repoName = repo.left(repo.size() - 10);
        }

        // Not using libzypp to create repo files because it does not support
        // file name being different than the repo name/alias (ssu_ prefix)
        QString repoFilePath = QString("%1/ssu_%2_%3.repo")
                               .arg(Sandbox::map(ZYPP_REPO_PATH))
                               .arg(repo)
                               .arg(rndMode ? "rnd" : "release");

        if (url(repoName, rndMode).isEmpty()) {
            // TODO, repositories should only be disabled if they're not required
            //       for this machine. For required repositories error is better
            QTextStream qerr(stderr);
            qerr << "Repository " << repo << " does not contain valid URL, skipping and disabling." << endl;
            disable(repo);
            QFile(repoFilePath).remove();
            continue;
        }

        QFile repoFile(repoFilePath);

        if (repoFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream out(&repoFile);
            // TODO, add -rnd or -release if we want to support having rnd and
            //       release enabled at the same time
            out << "[" << repo << "]" << endl
                << "name=" << repo << endl
                << "failovermethod=priority" << endl
                << "type=rpm-md" << endl
                << "gpgcheck=0" << endl
                << "enabled=1" << endl;

            if (rndMode)
                out << "baseurl=plugin:ssu?rnd&repo=" << repoName << debugSplit << endl;
            else
                out << "baseurl=plugin:ssu?repo=" << repoName << debugSplit << endl;

            out.flush();
        }
    }
}

QStringList SsuRepoManager::repoVariables(QHash<QString, QString> *storageHash, bool rnd)
{
    SsuVariables var;
    SsuCoreConfig *settings = SsuCoreConfig::instance();
    QStringList configSections;
    SsuSettings repoSettings(SSU_REPO_CONFIGURATION, SSU_REPO_CONFIGURATION_DIR);

    // fill in all arbitrary repo specific variables from ssu.ini
    var.variableSection(settings, "repository-url-variables", storageHash);

    // fill in all global variables from ssu.ini
    // TODO: should be handled somewhere in core variable logic once variables
    //       are more widely used outside of repository urls, for now this is
    //       just for easier migration to "full" variable usage at a later point.
    var.variableSection(settings, "global-variables", storageHash);

    // add/overwrite some of the variables with sane ones
    if (rnd) {
        storageHash->insert("flavour",
                            repoSettings.value(
                                settings->flavour() + "-flavour/flavour-pattern").toString());
        storageHash->insert("flavourPattern",
                            repoSettings.value(
                                settings->flavour() + "-flavour/flavour-pattern").toString());
        storageHash->insert("flavourName", settings->flavour());
        configSections << settings->flavour() + "-flavour" << "rnd" << "all";

        // Make it possible to give any values with the flavour as well.
        // These values can be overridden later with domain if needed.
        var.variableSection(&repoSettings, settings->flavour() + "-flavour", storageHash);
    } else {
        configSections << "release" << "all";
    }

    storageHash->insert("release", settings->release(rnd));

    if (!storageHash->contains("debugSplit"))
        storageHash->insert("debugSplit", "packages");

    if (!storageHash->contains("arch"))
        storageHash->insert("arch", settings->value("arch").toString());

    return configSections;
}

// RND repos have flavour (devel, testing, release), and release (latest, next)
// Release repos only have release (latest, next, version number)
QString SsuRepoManager::url(const QString &repoName, bool rndRepo,
                            QHash<QString, QString> repoParameters,
                            QHash<QString, QString> parametersOverride)
{
    SsuDeviceInfo deviceInfo;

    // set debugSplit for incorrectly configured debuginfo repositories (debugSplit
    // should already be passed by the url resolver); might be overriden later on,
    // if required
    if (repoName.endsWith("-debuginfo") && !repoParameters.contains("debugSplit"))
        repoParameters.insert("debugSplit", "debug");

    QStringList configSections = repoVariables(&repoParameters, rndRepo);

    // Override device model (and therefore all the family, ... stuff)
    if (parametersOverride.contains("model"))
        deviceInfo.setDeviceModel(parametersOverride.value("model"));

    repoParameters.insert("deviceFamily", deviceInfo.deviceFamily());
    repoParameters.insert("deviceModel", deviceInfo.deviceModel());
    repoParameters.insert("deviceVariant", deviceInfo.deviceVariant(true));

    QString adaptationRepoName = deviceInfo.adaptationVariables(repoName, &repoParameters);

    SsuCoreConfig *settings = SsuCoreConfig::instance();
    QString domain;

    if (parametersOverride.contains("domain")) {
        domain = parametersOverride.value("domain");
        domain.replace("-", ":");
    } else {
        domain = settings->domain();
    }

    repoParameters.insert("brand", settings->brand());

    // variableSection does autodetection for the domain default section
    SsuSettings repoSettings(SSU_REPO_CONFIGURATION, SSU_REPO_CONFIGURATION_DIR);
    SsuVariables var;
    var.variableSection(&repoSettings, domain + "-domain", &repoParameters);

    // override arbitrary variables, mostly useful for generating mic URLs
    QHash<QString, QString>::const_iterator i = parametersOverride.constBegin();
    while (i != parametersOverride.constEnd()) {
        repoParameters.insert(i.key(), i.value());
        i++;
    }

    // search for URLs for repositories. Lookup order is:
    // 1. overrides in ssu.ini
    // 2. URLs from features
    // 3. URLs from repos.ini

    SsuFeatureManager featureManager;
    QString r;

    if (settings->contains("repository-urls/" + adaptationRepoName)) {
        r = settings->value("repository-urls/" + adaptationRepoName).toString();
    } else if (!featureManager.url(adaptationRepoName, rndRepo).isEmpty()) {
        r = featureManager.url(adaptationRepoName, rndRepo);
    } else {
        foreach (const QString &section, configSections) {
            repoSettings.beginGroup(section);
            if (repoSettings.contains(adaptationRepoName)) {
                r = repoSettings.value(adaptationRepoName).toString();
                repoSettings.endGroup();
                break;
            }
            repoSettings.endGroup();
        }
    }

    return var.resolveString(r, &repoParameters);
}
