#include <iostream>
#include <vector>
#include "output.h"

using namespace std;

int main() {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    // 模拟调度结果（测试 Output 模块用）
    vector<ScheduleRecord> test_records = {
        {2, 1, 5, 4, 15},
        {1, 2, 0, 2, 10},
        {3, 1, 8, 2, 18}
    };

    writeScheduleRecords(cout, test_records);

    return 0;
}