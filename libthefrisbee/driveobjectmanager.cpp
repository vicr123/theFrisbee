/****************************************
 *
 *   INSERT-PROJECT-NAME-HERE - INSERT-GENERIC-NAME-HERE
 *   Copyright (C) 2020 Victor Tran
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * *************************************/
#include "driveobjectmanager.h"

#include "DriveObjects/diskobject.h"
#include "DriveObjects/partitiontableinterface.h"
#include "DriveObjects/driveinterface.h"
#include <QDebug>
#include <QTimer>

#include <QDBusMessage>
#include <QDBusConnection>
#include <QDBusObjectPath>
#include <QDBusArgument>

struct DriveObjectManagerPrivate {
    QMap<QDBusObjectPath, DiskObject*> objects;
    QMap<QDBusObjectPath, DriveInterface*> drives;
};

DriveObjectManager::DriveObjectManager(QObject* parent) : QObject(parent) {
    d = new DriveObjectManagerPrivate();

    QDBusConnection::systemBus().connect("org.freedesktop.UDisks2", "/org/freedesktop/UDisks2", "org.freedesktop.DBus.ObjectManager", "InterfacesAdded", this, SLOT(updateInterfaces()));
    QDBusConnection::systemBus().connect("org.freedesktop.UDisks2", "/org/freedesktop/UDisks2", "org.freedesktop.DBus.ObjectManager", "InterfacesRemoved", this, SLOT(updateInterfaces()));
    updateInterfaces();
}

DriveObjectManager* DriveObjectManager::instance() {
    static DriveObjectManager* manager = new DriveObjectManager();
    return manager;
}

QList<DiskObject*> DriveObjectManager::rootDisks() {
    QList<DiskObject*> disks = instance()->d->objects.values();
    QSet<DiskObject*> notRootDisks;

    for (DiskObject* disk : disks) {
        PartitionTableInterface* partitionTable = disk->interface<PartitionTableInterface>();
        if (partitionTable) {
            for (DiskObject* partition : partitionTable->partitions()) {
                notRootDisks.insert(partition);
            }
        }
    }

    for (DiskObject* disk : notRootDisks) {
        disks.removeOne(disk);
    }

    return disks;
}

DiskObject* DriveObjectManager::diskForPath(QDBusObjectPath path) {
    return instance()->d->objects.value(path);
}

DriveInterface* DriveObjectManager::driveForPath(QDBusObjectPath path) {
    return instance()->d->drives.value(path);
}

void DriveObjectManager::updateInterfaces() {

    QDBusMessage managedObjects = QDBusMessage::createMethodCall("org.freedesktop.UDisks2", "/org/freedesktop/UDisks2", "org.freedesktop.DBus.ObjectManager", "GetManagedObjects");
    QMap<QDBusObjectPath, QMap<QString, QVariantMap>> reply;
    QDBusArgument arg = QDBusConnection::systemBus().call(managedObjects).arguments().first().value<QDBusArgument>();

    arg >> reply;

    for (QDBusObjectPath path : reply.keys()) {
        auto interfaces = reply.value(path);

        //Determine which type of object to create
        if (path.path().startsWith("/org/freedesktop/UDisks2/block_devices")) {
            DiskObject* object;
            if (d->objects.contains(path)) {
                object = d->objects.value(path);
            } else {
                object = new DiskObject(path);
                d->objects.insert(path, object);
                QTimer::singleShot(0, [ = ] {
                    emit diskAdded(object);
                });
            }

            object->updateInterfaces(interfaces);
        } else if (path.path().startsWith("/org/freedesktop/UDisks2/drives")) {
            DriveInterface* object;
            if (d->drives.contains(path)) {
                object = d->drives.value(path);
            } else {
                object = new DriveInterface(path);
                d->drives.insert(path, object);
                QTimer::singleShot(0, [ = ] {
                    emit driveAdded(object);
                });
            }

            object->updateProperties(interfaces.value("org.freedesktop.UDisks2.Drive"));
        }
    }

    QList<QDBusObjectPath> gone;
    for (QDBusObjectPath path : d->objects.keys()) {
        if (!reply.contains(path)) gone.append(path);
    }
    for (QDBusObjectPath path : d->drives.keys()) {
        if (!reply.contains(path)) gone.append(path);
    }

    for (QDBusObjectPath path : gone) {
        if (d->objects.contains(path)) {
            DiskObject* disk = d->objects.take(path);
            QTimer::singleShot(0, [ = ] {
                emit diskAdded(disk);
            });
            disk->deleteLater();
        } else {
            DriveInterface* drive = d->drives.take(path);
            QTimer::singleShot(0, [ = ] {
                emit driveAdded(drive);
            });
            drive->deleteLater();
        }
    }
}
