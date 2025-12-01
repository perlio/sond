/*
 sond (sond_checkbox.c) - Akten, Beweisstücke, Unterlagen
 Copyright (C) 2025  pelo america

 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU Affero General Public License as
 published by the Free Software Foundation, either version 3 of the
 License, or (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU Affero General Public License for more details.

 You should have received a copy of the GNU Affero General Public License
 along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef SRC_SOND_LOG_H_
#define SRC_SOND_LOG_H_

#define message(msg) g_log("Sond", G_LOG_LEVEL_MESSAGE, \
		"%s:%d (%s): %s", __FILE__, __LINE__, __func__, msg);

#define warning(msg) g_log("Sond", G_LOG_LEVEL_WARNING, \
		"%s:%d (%s): %s", __FILE__, __LINE__, __func__, msg);

#define critical(msg) g_log("Sond", G_LOG_LEVEL_CRITICAL, \
		"%s:%d (%s): %s", __FILE__, __LINE__, __func__, msg);

#define error(msg) g_log("Sond", G_LOG_LEVEL_ERROR, \
		"%s:%d (%s): %s", __FILE__, __LINE__, __func__, msg);

#endif /* SRC_SOND_LOG_H_ */
