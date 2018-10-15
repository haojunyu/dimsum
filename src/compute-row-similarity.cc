#include "dimsumv2.hpp"
#include "boost/program_options.hpp"
#include <iostream>
#include <string>

using namespace std;

int main(int argc, char** argv) {
    namespace po = boost::program_options;
    std::string desc_text = "compute row vector pairs' similarity.";
    po::options_description desc(desc_text);
    desc.add_options()
        ("help,h", "show this message")
        ("data,d", po::value<std::string>(), "matrix data file")
        ("output,o", po::value<std::string>(), "outout similarity to")
        ("mirror",  "output similarity <i, j> and <j, i> same time")
        ("threshold,t", po::value<float>()->default_value(0.2), "similarity threshold");

    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);
    if (vm.count("help")
            || vm.count("data")==0
            || vm.count("output") == 0) {
        cout << desc << endl;
        return 0;
    }
    bool mirror = false;
    if (vm.count("mirror"))
        mirror = true;
    std::string matrix_data = vm["data"].as<std::string>();
    std::string output = vm["output"].as<std::string>();
    float threshold = vm["threshold"].as<float>();
    cout << "[0] start loading matrix." << endl;
    aisearch::PairSimilarityCaculator caculator(matrix_data.c_str(), threshold);
    cout << "[1] loading matrix finished." << endl;
    caculator.Caculate(output.c_str(), mirror);
    cout << "[2] caculating similarity finished." << endl;
    return 0;
}