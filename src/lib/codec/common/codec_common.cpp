/*
 *    Copyright (C) 2016-2023 Grok Image Compression Inc.
 *
 *    This source code is free software: you can redistribute it and/or  modify
 *    it under the terms of the GNU Affero General Public License, version 3,
 *    as published by the Free Software Foundation.
 *
 *    This source code is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU Affero General Public License for more details.
 *
 *    You should have received a copy of the GNU Affero General Public License
 *    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <condition_variable>
using namespace std::chrono_literals;

namespace grk
{
std::condition_variable sleep_cv;
std::mutex sleep_cv_m;
// val == # of 100ms increments to wait
int batch_sleep(int val)
{
    std::unique_lock<std::mutex> lk(sleep_cv_m);
    sleep_cv.wait_for(lk, val * 100ms, [] { return false; });
    return 0;
};

}
