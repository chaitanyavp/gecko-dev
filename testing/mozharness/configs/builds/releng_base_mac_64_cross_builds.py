import os

config = {
    #########################################################################
    ######## MACOSX CROSS GENERIC CONFIG KEYS/VAlUES

    # note: overridden by MOZHARNESS_ACTIONS in TaskCluster tasks
    'default_actions': [
        'build',
    ],
    'app_ini_path': '%(obj_dir)s/dist/bin/application.ini',
    'secret_files': [
        {'filename': '/builds/gapi.data',
         'secret_name': 'project/releng/gecko/build/level-%(scm-level)s/gapi.data',
         'min_scm_level': 1},
        {'filename': '/builds/mozilla-desktop-geoloc-api.key',
         'secret_name': 'project/releng/gecko/build/level-%(scm-level)s/mozilla-desktop-geoloc-api.key',
         'min_scm_level': 2, 'default': 'try-build-has-no-secrets'},
    ],
    'enable_check_test': False,
    'vcs_share_base': '/builds/hg-shared',
    #########################################################################


    #########################################################################
    ###### 64 bit specific ######
    'platform': 'macosx64',
    'stage_platform': 'macosx64',
    'env': {
        'MOZBUILD_STATE_PATH': os.path.join(os.getcwd(), '.mozbuild'),
        'HG_SHARE_BASE_DIR': '/builds/hg-shared',
        'MOZ_OBJDIR': '%(abs_obj_dir)s',
        'TINDERBOX_OUTPUT': '1',
        'TOOLTOOL_CACHE': '/builds/worker/tooltool-cache',
        'TOOLTOOL_HOME': '/builds',
        'MOZ_CRASHREPORTER_NO_REPORT': '1',
        'LC_ALL': 'C',
        ## 64 bit specific
        'PATH': '/usr/local/bin:/bin:'
                '/usr/bin:/usr/local/sbin:/usr/sbin:/sbin'
        ##
    },
    "check_test_env": {
        'MINIDUMP_STACKWALK': '%(abs_tools_dir)s/breakpad/linux64/minidump_stackwalk',
        'MINIDUMP_SAVE_PATH': '%(base_work_dir)s/minidumps',
    },
    'mozconfig_platform': 'macosx64',
    'mozconfig_variant': 'nightly',
    'artifact_flag_build_variant_in_try': 'cross-artifact',
    #########################################################################
}
