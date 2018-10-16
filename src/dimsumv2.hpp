/*
 * Copyright (C) 2018- xingwudao
 *
 * Author:
 *	xingwudao
 *
 * Source:
 *	https://github.com/xingwudao/dimsum
 *
 * This file is a part of dimsum tool
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 */

#ifndef SRC_DIMSUMV2_HPP
#define SRC_DIMSUMV2_HPP

#include <fstream>
#include <omp.h>
#include <iostream>
#include <vector>
#include <cmath>
#include <ctime>
#include <random>
#include <utility>
#include <sstream>
#include <iomanip>
#include <string>
#include <unordered_set>
#include <cstdint>

using std::vector;
using std::unordered_set;
using std::string;
using std::pair;
using std::cout;
using std::cin;
using std::setprecision;
using std::ifstream;
using std::ofstream;
using std::ostringstream;
using std::endl;
using std::sqrt;
using std::log;
using std::cerr;
using std::default_random_engine;
using std::uniform_real_distribution;

namespace kaijiang {

typedef vector< pair<uint32_t, float> > SparseVector;

// 计算行向量两两相似度的DIMSUM算法
class PairSimilarityCaculator {
    public:
        PairSimilarityCaculator(const char* data_file,
                float threshold = 0.1) {
            threshold_ = threshold;
            LoadMatrix(data_file);
        }

        virtual ~PairSimilarityCaculator() {
        }

        // 加载COO格式矩阵
        // 参数：
        //     data_file -- 数据文件，COO格式稀疏矩阵,第一行是行列数及非零元素数
        // 返回：
        //     非零元素个数
        uint32_t LoadMatrix(const char* data_file) {
            matrix_.clear();
            row_vector_mod_.clear();
            similarity_.clear();
            m_ = -1;
            n_ = -1;
            total_ = 0;
            ifstream fin(data_file);
            if (!fin.is_open()) {
                return 0;
            }

            uint32_t m = 0;
            uint32_t n = 0;
            float cell = 0.0;
            // 矩阵的行和列，及非零元素数
            fin >> m_ >> n_ >> total_;
            SparseVector cells_of_column_vector;
            row_vector_mod_ =  vector<float>(m_, 0.0);
            matrix_ = vector<SparseVector>(n_, cells_of_column_vector);
            // m和n从0开始编号
            while (fin >> m >> n >> cell) {
                matrix_[n].push_back(pair<uint32_t, float>(m, cell));
                row_vector_mod_[m] += cell * cell;
            }
            // 作为矩阵的维度，需要自增1
            return total_;
        }

        // 计算行向量的两两相似度
        // 参数：
        //     output_name: 文件名
        //     mirror: 一对相似向量是否保存两个相似记录，
        //             例如i和j相似度为0,8，会保存两行：
        //             i j 0.8
        //             j i 0.8
        // 返回：
        //     阈值之内的结果数
        //
        uint32_t Caculate(const char* output_name, bool mirror) {
            // m个行向量之间相似度，是一个对角方形矩阵，所以只用保存下三角即可
            // (i,k)的相似度保存在i * (i - 1)/2 + k，i比k大。如果查询时i比k小，
            // 即查询上三角阵，则交换i和k再查询下三角阵。
            // 注意，这里不保存对角线上的值，对角线上就是自己和自己的相似度，就是1
            uint64_t cells = 1 + (m_ + 1) * (m_ - 2) / 2; // 下三角阵总共元素数
            similarity_ = vector<float>(cells, 0.0);

            float gama = 4 * log((float)m_) / (threshold_ + 0.0001);
            gama = sqrt(gama);

            int thread_number = omp_get_num_threads();
            bool parallelism_enabled = true;
#ifdef DISABLE_OMP
            parallelism_enabled = false;
            thread_number = 1;
#endif
            // 计算行向量的模
            if(thread_number > row_vector_mod_.size())
                thread_number = row_vector_mod_.size();

            // row_vector_mod_ 是行向量的模，反映了用户评价物品的个数,在map阶段需要使用
            // row_vector_mod_v2 用来最后计算相似度时做分母用的
            vector<float> row_vector_mod_v2 = row_vector_mod_;
#pragma omp parallel for num_threads(thread_number) if(parallelism_enabled)
            for (auto i = 0; i < row_vector_mod_.size(); i++) {
                row_vector_mod_[i] = sqrt(row_vector_mod_[i]);
                row_vector_mod_v2[i] = row_vector_mod_[i];

                // 参见论文公式中的分母，取gama和行向量模较小那个
                if (row_vector_mod_v2[i] > gama)
                    row_vector_mod_v2[i] = gama;
                if (row_vector_mod_v2[i] < 0.0001)
                    row_vector_mod_v2[i] = 0.0001;
            }

            // 随机数发生器
            default_random_engine generator;
            uniform_real_distribution<float> distribution(0.0, 1.0);

            // 为了让并行时不冲突，每一个线程保存一份结果，最后再合并
            vector<vector<float> > to_reduction(thread_number, similarity_);

            // 遍历每一个列向量
            // 对列向量并行
            cout << "    computing similarity (map phase) start." << endl;
            if(thread_number > matrix_.size())
                thread_number = matrix_.size();
#pragma omp parallel for num_threads(thread_number) if(parallelism_enabled)
            for (auto l = 0; l < matrix_.size(); l++) {
                // 每个线程各自计算，各自保存一份求和结果
                int thread_index = omp_get_thread_num();
                // 遍历列向量中的非零元素
                for (auto i = 0; i < matrix_[l].size(); i++) {
                    uint32_t index_i = matrix_[l][i].first;
                    float value_i = matrix_[l][i].second;

                    // 以概率min(1, $\frac{\sqrt{\gamma}}{||r_i||}$ )
                    // 决定要不要将行向量i加入计算
                    float prob =  gama/row_vector_mod_[index_i];
                    prob = prob > 1.0 ? 1.0 : prob;
                    float number = distribution(generator);
                    if (number < prob) {
                        for (auto k = i + 1; k <  matrix_[l].size(); k++) {
                            uint32_t index_k = matrix_[l][k].first;
                            float value_k = matrix_[l][k].second;
                            // 以概率min(1, $\frac{\sqrt{\gamma}}{||r_k||}$ )
                            // 决定要不要将行向量k和行向量i的在当前元素的成绩用于计算相似度
                            prob =  gama/row_vector_mod_[index_k];
                            prob = prob > 1.0 ? 1.0 : prob;
                            number = distribution(generator);
                            if (number < prob) {
                                // 始终只保存下三角阵：
                                uint32_t li = index_i;
                                uint32_t lk = index_k;
                                if (index_i < index_k) {
                                    li = index_k;
                                    lk = index_i;
                                }
                                uint64_t index_similarity = li * (li - 1)/2 + lk;
                                float emit_value = value_i * value_k;
                                to_reduction[thread_index][index_similarity]
                                                      += emit_value;
                            }
                        }
                    }
                }
            }

            cout << "    computing similarity (map phase) end." << endl;
            cout << "    computing similarity (reduce phase) and saving start." << endl;
            // 将各个线程并行计算的结果合并为最终相似度
            vector<std::ostringstream*> output_streams;
            for (auto i = 0; i < thread_number; i++) {
                output_streams.push_back(new ostringstream);
            }
#pragma omp parallel for num_threads(thread_number) if(parallelism_enabled)
            for (uint64_t n = 0; n < cells; n++) {
                for (auto j = 0; j < thread_number; j++){
                    similarity_[n] += to_reduction[j][n];
                }
                if (similarity_[n] < 0.0001)
                    continue;
                // 反向计算下三角阵中的坐标<i, j>, 其中i > j, 注意不包含对角线。
                // 计算方法是：
                // 1. 下三角阵累积到当前第i行末尾一共有多少个元素? 假设是n个，那么这时n = i(i+1)/2；
                // 2. 计算对应的i（解一个一元二次方程，取正根），为(sqrt(8*n)-1)/2
                // 3. 实际上n不一定对应第i行的末尾，所以i会是一个非整数，所以加1取整i = uint32_t((sqrt(8*(double)n)-1)/2 + 1)
                // 4. 根据原来下三角阵下标计算方法：n = i(i-1)/2 + j，计算得到j
                uint32_t i = uint32_t((sqrt(8*(double)n)-1)/2 + 1);
                uint32_t j = n - i * (i - 1)/2;
                similarity_[n] /= row_vector_mod_v2[i] * row_vector_mod_v2[j];
                if (similarity_[n] < threshold_)
                    continue;
                int thread_index = omp_get_thread_num();
                (*(output_streams[thread_index])) << std::fixed
                            << setprecision(2)
                            << i << " " << j << " "
                            << similarity_[n] << "\n";
                if (mirror) {
                    (*(output_streams[thread_index])) << std::fixed
                                << setprecision(2)
                                << j << " " << i << " "
                                << similarity_[n] << "\n";
                }
            }
            // 使用字符串流写入文件比较慢，
            // 采用了内存中存下写入文件内容(可以并行拼接)，
            // 最后以二进制同时写入的方式
            ofstream fout(output_name, ofstream::binary);
            if (!fout.is_open()) {
                cerr << output_name << " open failed." << endl;
                return 0;
            }
            for (auto i = 0; i < thread_number; i++) {
                fout.write(output_streams[i]->str().c_str(),
                        output_streams[i]->str().size());
                delete output_streams[i];
            }
            fout.close();

            cout << "    computing similarity (reduce phase) and saving end." << endl;
        }
    private:
        float threshold_; // 相似度阈值
        vector<SparseVector> matrix_; // 列向量
        vector<float> row_vector_mod_;    // 行向量的模
        uint32_t m_; // 行
        uint32_t n_; // 列
        uint32_t total_; // 非零元素个数
        vector<float> similarity_; // 相似度结果
};
}  // namespace kaijiang
#endif  // SRC_DIMSUMV2_HPP
