# -*- coding: utf-8 -*-
#
# English Language RTD & Sphinx config file
#
# Uses ../conf_common.py for most non-language-specific settings.

# Importing conf_common adds all the non-language-specific
# parts to this conf module
import sys
import os
sys.path.insert(0, os.path.abspath('..'))
from conf_common import *  # noqa: F401, F403 - need to make available everything from common

# General information about the project.
project = u'ESP-SR 用户指南'
copyright = u'2016 - 2022 乐鑫信息科技（上海）股份有限公司'

pdf_title = u'ESP-SR 用户指南'
# Final PDF filename will contains target and version
pdf_file_prefix = u'esp-sr'

# The language for content autogenerated by Sphinx. Refer to documentation
# for a list of supported languages.
language = 'zh_CN'
