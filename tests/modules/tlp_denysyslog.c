/*
 * tlp-syslog.c
 *
 * TALPA Filesystem Interceptor
 *
 * Copyright (C) 2004 Sophos Plc, Oxford, England.
 *
 * This program is free software; you can redistribute it and/or modify it under the terms of the
 * GNU General Public License Version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without
 * even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program; if not,
 * write to the Free Software Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/fs.h>
#include <linux/unistd.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <asm/uaccess.h>
#include <asm/errno.h>

#include "tlp-test.h"


#include "common/bool.h"
#define TALPA_SUBSYS "syslogtest"
#include "common/talpa.h"

#include "components/services/linux_personality_impl/linux_personality.h"
#include "components/services/linux_filesystem_impl/linux_fileinfo.h"
#include "components/services/linux_filesystem_impl/linux_filesysteminfo.h"
#include "components/core/intercept_filters_impl/deny_syslog/deny_syslog.h"
#include "components/core/intercept_processing_impl/evaluation_report_impl.h"
#include "components/services/configurator_impl/procfs_configurator.h"
#include "components/services/linux_filesystem_impl/linux_systemroot.h"
#include "app_ctrl/iportability_app_ctrl.h"

static ISystemRoot* systemRoot(void);
static LinuxSystemRoot* mSystemRoot;

/*
 * Singleton Object.
 */
static IPortabilityApplicationControl GL_talpa_linux =
    {
        NULL,
        systemRoot,
        NULL,
        NULL,
        NULL
    };

const IPortabilityApplicationControl* TALPA_Portability(void)
{
    return &GL_talpa_linux;
}

static ISystemRoot* systemRoot(void)
{
    return &mSystemRoot->i_ISystemRoot;
}

DenySyslogFilter *syslog;
EvaluationReportImpl *erep;
static ProcfsConfigurator*  mConfig;

int talpa_ioctl(struct inode *inode, struct file *file, unsigned int cmd, unsigned long parm)
{
    int ret = -ENOTTY;

    LinuxPersonality *pers;

    struct talpa_file tf;
    LinuxFileInfo *fi;

    struct talpa_filesystem tfs;
    LinuxFilesystemInfo *fsi;

    pers = newLinuxPersonality();

    switch ( cmd )
    {
        case TALPA_TEST_SET_EVAL_CODE:
            erep->i_IEvaluationReport.setRecommendedAction(erep,parm);
            ret = 0;
            break;
        case TALPA_TEST_FILEINFO:
            ret = copy_from_user(&tf, (void *)parm, sizeof(struct talpa_file));
            if ( !ret )
            {
                fi = newLinuxFileInfo(tf.operation, tf.name, 0, 0);
                if ( fi )
                {
                    syslog->i_IInterceptFilter.examineFile(syslog, &erep->i_IEvaluationReport, &pers->i_IPersonality, &fi->i_IFileInfo, NULL);
                    fi->delete(fi);
                }
                else
                {
                    err("Failed to create IFileInfo!");
                    ret = -EINVAL;
                }
            }
            else
            {
                err("copy_from_user!");
            }
            break;
        case TALPA_TEST_FILESYSTEMINFO:
            ret = copy_from_user(&tfs, (void *)parm, sizeof(struct talpa_filesystem));
            if ( !ret )
            {
                fsi = newLinuxFilesystemInfo(tfs.operation, tfs.dev, tfs.target, tfs.type);
                if ( fsi )
                {
                    syslog->i_IInterceptFilter.examineFilesystem(syslog, &erep->i_IEvaluationReport, &pers->i_IPersonality, &fsi->i_IFilesystemInfo);
                    fsi->delete(fsi);
                }
                else
                {
                    err("Failed to create IFilesystemInfo!");
                    ret = -EINVAL;
                }
            }
            else
            {
                err("copy_from_user!");
            }
            break;
    }

    pers->delete(pers);

    return ret;
}

struct file_operations talpa_fops =
{
    owner:  THIS_MODULE,
    ioctl:  talpa_ioctl

};

static int __init talpa_test_init(void)
{
    int ret;

    mSystemRoot = newLinuxSystemRoot();
    if (mSystemRoot == 0)
    {
        err("Failed to create system root");
        return -ENOMEM;
    }

    mConfig = newProcfsConfigurator();
    if ( !mConfig )
    {
        err("Failed to allocate configurator");
        mSystemRoot->delete(mSystemRoot);
        return -ENOMEM;
    }

    syslog = newDenySyslogFilter("DenySyslog");

    if ( !syslog )
    {
        mConfig->delete(mConfig);
        mSystemRoot->delete(mSystemRoot);
        err("Failed to create deny syslog filter!");
        return 1;
    }

    erep = newEvaluationReportImpl(0);

    if ( !erep )
    {
        err("Failed to create evaluation report!");
        mConfig->delete(mConfig);
        syslog->delete(syslog);
        mSystemRoot->delete(mSystemRoot);
        return 1;
    }


    mConfig->i_IConfigurator.attach(mConfig, ECG_InterceptFilter, &syslog->i_IConfigurable);

    ret = register_chrdev(TALPA_MAJOR, TALPA_DEVICE, &talpa_fops);

    if ( ret )
    {
        err("Failed to register TALPA Test character device!");
        mConfig->i_IConfigurator.detach(mConfig, &syslog->i_IConfigurable);
        erep->delete(erep);
        syslog->delete(syslog);
        mSystemRoot->delete(mSystemRoot);
        return ret;
    }

    return 0;
}

static void __exit talpa_test_exit(void)
{
    int ret;

    mConfig->i_IConfigurator.detach(mConfig, &syslog->i_IConfigurable);
    mConfig->delete(mConfig);
    erep->delete(erep);
    syslog->delete(syslog);
    mSystemRoot->delete(mSystemRoot);

    ret = unregister_chrdev(TALPA_MAJOR, TALPA_DEVICE);

    if ( ret )
    {
        err("Hmmmmmm... very strange things are happening!");
    }
}

/*
 *
 * Module information.
 *
 */
MODULE_AUTHOR("Sophos Plc");
MODULE_DESCRIPTION("TALPA Filesystem Interceptor Test Module");
MODULE_LICENSE("GPL");

module_init(talpa_test_init);
module_exit(talpa_test_exit);

