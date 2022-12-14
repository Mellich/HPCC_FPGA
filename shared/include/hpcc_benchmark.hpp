/*
Copyright (c) 2020 Marius Meyer

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/
#ifndef SHARED_HPCC_BENCHMARK_HPP_
#define SHARED_HPCC_BENCHMARK_HPP_

#include <memory>
#include <iostream>

/* External library headers */
#ifdef USE_DEPRECATED_HPP_HEADER
#include "CL/cl.hpp"
#else
#include OPENCL_HPP_HEADER
#endif
#ifdef _USE_MPI_
#include "mpi.h"
#endif

/* Project's headers */
#include "setup/fpga_setup.hpp"
#include "cxxopts.hpp"
#include "nlohmann/json.hpp"
#include "parameters.h"
#include "communication_types.hpp"

#define STR_EXPAND(tok) #tok
#define STR(tok) STR_EXPAND(tok)

#define VALUE_SPACE 11
#define UNIT_SPACE 8
#define ENTRY_SPACE (VALUE_SPACE + UNIT_SPACE + 1)

using json = nlohmann::json;

/**
 * @brief Contains all classes and functions that are used as basis
 *          for all benchmarks.
 * 
 */
namespace hpcc_base {

class HpccResult {
public:
    double value;
    std::string unit;

    HpccResult(double value, std::string unit): value(value), unit(unit) {}
    
    friend std::ostream &operator<<(std::ostream &os, const HpccResult &result) {
        os << std::setw(VALUE_SPACE) << result.value << " " << std::left << std::setw(UNIT_SPACE) << result.unit << std::right;
        return os;
    }

    std::string to_string() const {
        std::ostringstream oss;
        oss << *this;
        return oss.str();
    }
    // TODO: to_json function
};

/**
 * @brief This class should be derived and extended for every benchmark.
 *          It is a pure data object containing the benchmark settings that are
 *          used to execute the benchmark kernel.
 *       
 */
class BaseSettings {

public:

    /**
     * @brief Number of times the kernel execution will be repeated
     * 
     */
    uint numRepetitions;

    /**
     * @brief Boolean showing if memory interleaving is used that is 
     *          triggered from the host side (Intel specific)
     * 
     */
    bool useMemoryInterleaving;

    /**
     * @brief Boolean showing if the output data of the benchmark kernel
     *          should be validated or not
     * 
     */
    bool skipValidation;

    /**
     * @brief The default platform that should be used for execution. 
     *          A number representing the index in the list of available platforms
     * 
     */
    int defaultPlatform;

    /**
     * @brief The platform string of the platform that should be used
     *
     */
    std::string platformString;

    /**
     * @brief The default device that should be used for execution. 
     *          A number representing the index in the list of available devices
     * 
     */
    int defaultDevice;

    /**
     * @brief Path to the kernel file that is used for execution
     * 
     */
    std::string kernelFileName;

    /**
     * @brief Number of times the kernel is replicated
     * 
     */
    uint kernelReplications;

    /**
     * @brief Only test the given configuration. Do not execute the benchmarks
     * 
     */
    bool testOnly;
    
    std::string dumpfilePath;

    /**
     * @brief Type of inter-FPGA communication used
     * 
     */
    CommunicationType communicationType;

    /**
     * @brief Construct a new Base Settings object
     * 
     * @param results The resulting map from parsing the program input parameters
     */
    BaseSettings(cxxopts::ParseResult &results) : numRepetitions(results["n"].as<uint>()),
#ifdef INTEL_FPGA
            useMemoryInterleaving(static_cast<bool>(results.count("i"))), 
#else
            useMemoryInterleaving(true),
#endif
            skipValidation(static_cast<bool>(results.count("skip-validation"))), 
            defaultPlatform(results["platform"].as<int>()),
            defaultDevice(results["device"].as<int>()),
            platformString(results["platform_str"].as<std::string>()),
            kernelFileName(results["f"].as<std::string>()),
#ifdef NUM_REPLICATIONS
            kernelReplications(results.count("r") > 0 ? results["r"].as<uint>() : NUM_REPLICATIONS),
#else
            kernelReplications(results.count("r") > 0 ? results["r"].as<uint>() : 1),
#endif
#ifdef COMMUNICATION_TYPE_SUPPORT_ENABLED
            communicationType(retrieveCommunicationType(results["comm-type"].as<std::string>(), results["f"].as<std::string>())),
#else
            communicationType(retrieveCommunicationType("UNSUPPORTED", results["f"].as<std::string>())),
#endif
            dumpfilePath(results["dump-json"].as<std::string>()),
            testOnly(static_cast<bool>(results.count("test"))) {}

    /**
     * @brief Get a map of the settings. This map will be used to print the final configuration.
     *          Derived classes should override it to add additional configuration options
     * 
     * @return std::map<std::string,std::string> 
     */
    virtual std::map<std::string,std::string> getSettingsMap() {
    int mpi_size = 0;
#ifdef _USE_MPI_
     MPI_Comm_size(MPI_COMM_WORLD, &mpi_size);
#endif
    std::string str_mpi_ranks = "None";
    if (mpi_size > 0) {
        str_mpi_ranks = std::to_string(mpi_size);
    }
        return {{"Repetitions", std::to_string(numRepetitions)}, {"Kernel Replications", std::to_string(kernelReplications)}, 
                {"Kernel File", kernelFileName}, {"MPI Ranks", str_mpi_ranks}, {"Test Mode", testOnly ? "Yes" : "No"},
                {"Communication Type", commToString(communicationType)}
#ifdef INTEL_FPGA
                ,{"Memory Interleaving", useMemoryInterleaving ? "Yes" : "No"}
#endif 
                };
    }

};


/**
 * @brief Settings class that is containing the program settings together with
 *          additional information about the OpenCL runtime
 * 
 * @tparam TSettings The program settings class that should be used (Must derive from BaseSettings)
 */
template <class TSettings>
class ExecutionSettings {
public:

    /**
     * @brief Pointer to the additional program settings
     * 
     */
    std::unique_ptr<TSettings> programSettings;

    /**
     * @brief The OpenCL device that should be used for execution
     * 
     */
    std::unique_ptr<cl::Device> device;

    /**
     * @brief The OpenCL context that should be used for execution
     * 
     */
    std::unique_ptr<cl::Context> context;

    /**
     * @brief The OpenCL program that contains the benchmark kernel
     * 
     */
    std::unique_ptr<cl::Program> program;

    /**
     * @brief Construct a new Execution Settings object
     * 
     * @param programSettings_ Pointer to an existing program settings object that is derived from BaseSettings
     * @param device_ Used OpenCL device
     * @param context_ Used OpenCL context
     * @param program_ Used OpenCL program
     */
    ExecutionSettings(std::unique_ptr<TSettings> programSettings_, std::unique_ptr<cl::Device> device_, 
                        std::unique_ptr<cl::Context> context_, std::unique_ptr<cl::Program> program_): 
                                    programSettings(std::move(programSettings_)), device(std::move(device_)), 
                                    context(std::move(context_)), program(std::move(program_)) {}

    /**
     * @brief Destroy the Execution Settings object. Used to specify the order the contained objects are destroyed 
     *         to prevent segmentation faults during exit.
     * 
     */
    ~ExecutionSettings() {
        program = nullptr;
        context = nullptr;
        device = nullptr;
        programSettings = nullptr;
    }

    std::string
    getDeviceName() const {
        std::string device_name;
        if (!programSettings->testOnly) {
            device->getInfo(CL_DEVICE_NAME, &device_name);
        } else {
            device_name = "TEST RUN: Not selected!";
        }
        return device_name;
    }
    
    /*
    std::string
    getPlatformName() const {
        std::string platform_name;
        if (!programSettings->testOnly) {
            platform->getInfo(CL_PLATFORM_NAME, &platform_name);
        } else {
            platform_name = "TEST RUN: Not selected!";
        }
        return platform_name;
    }
    */

};

/**
 * @brief Base benchmark class. Every benchmark should be derived from this class and implement its abstract methods.
 * 
 * @tparam TSettings Class used to represent the program settings of the benchmark
 * @tparam TData Class used to represent the benchmark input and output data
 * @tparam TOutput Class representing the measurements like timings etc
 */
template <class TSettings, class TData>
class HpccFpgaBenchmark {

private:

    /**
     * @brief This flag is set by the setupBenchmark() routine after successful setting up the benchmark.
     *          It is used to prevent undefined behavior in the case executeBenchmark() is called without
     *          a complete setup.
     * 
     */
    bool benchmark_setup_succeeded = false;

protected:

    /**
     * @brief The used execution settings that will be generated by setupBenchmark().
     *        It should be laos used by all other methods to read the current benchmark settings.
     * 
     */
    std::unique_ptr<ExecutionSettings<TSettings>> executionSettings;

    /**
     * @brief Add additional options to the program parameter parser
     * 
     * @param options The options object that will be used to parse the input parameters
     */
    virtual void
    addAdditionalParseOptions(cxxopts::Options &options) = 0;


    /**
     * @brief The communication rank of a MPI setup
     * 
     */
    int mpi_comm_rank = 0;

    /**
     * @brief The size of the MPI communication world
     * 
     */
    int mpi_comm_size = 1;

    /**
     * @brief This flag indicates if MPI was initialized before the object was constructed.
     *          In this case, no finalization will be done by the destuctor of the object.
     * 
     */
    bool mpi_external_init = true;
    
    /**
     *
     * @brief map containing the benchmark timings
     *
     */
    std::map<std::string, std::vector<double>> timings;

    /**
     *
     * @brief map containing the benchmark results
     *
     */
    std::map<std::string, HpccResult> results;
    
    /**
     *
     * @brief map containing the errors of the benchmark
     *
     */
    std::map<std::string, HpccResult> errors;

    /**
     * @brief This flag indicates whether the validation was successful
     *
     */
    bool validated = false;


public:

    /**
     * @brief Allocate and initiate the input data for the kernel
     * 
     * @return std::unique_ptr<TData> A data class containing the initialized data
     */
    virtual std::unique_ptr<TData>
    generateInputData() = 0;

    /**
     * @brief Execute the benchmark kernel and measure performance
     * 
     * @param data The initialized data for the kernel. It will be replaced by the kernel output for validation
     * @return std::unique_ptr<TOutput> A data class containing the measurement results of the execution
     */
    virtual void
    executeKernel(TData &data) = 0;

    /**
     * @brief Validate the output of the execution
     * 
     * @param data The output data after kernel execution
     * @return true If the validation is a success.
     * @return false If the validation failed
     */
    virtual bool
    validateOutput(TData &data) = 0;
    
    /**
     * @brief Print the error after validating output
    */
    virtual void
    printError() = 0;

    /**
     * @brief Collects the measurment results from all MPI ranks and 
     *      prints the measurement results of the benchmark to std::cout
     * 
     * @param output  The measurement data of the kernel execution
     */
    virtual void
    collectResults() = 0;
    
    virtual void
    printResults() = 0;

    /**
     * @brief Method that can be overwritten by inheriting classes to check the validity of input parameters.
     *          This can include checks of the input array sizes or if the given combination of parameters makes sense.
     *          It will be called at the end of the benchmark setup and the protected executionSettings should be used to
     *          do the required tests. The implementation of the function should print detailed information about possible 
     *          errors, if the validation fails.
     * 
     * @return true If the validation was a success
     * @return false If the validation failed.
     * 
     */
    virtual bool
    checkInputParameters() { return true;}

    /**
    * Parses and returns program options using the cxxopts library.
    * The parsed parameters are depending on the benchmark that is implementing
    * function.
    * The header file is used to specify a unified interface so it can also be used
    * in the testing binary.
    * cxxopts is used to parse the parameters.
    * @see https://github.com/jarro2783/cxxopts
    * 
    * @param argc Number of input parameters as it is provided by the main function
    * @param argv Strings containing the input parameters as provided by the main function
    * 
    * @return program settings that are created from the given program arguments
    */
    std::unique_ptr<TSettings>
    parseProgramParameters(int argc, char *argv[]) {
        std::stringstream ss;
        ss << PROGRAM_DESCRIPTION << std::endl;
#ifdef _USE_MPI_
        ss << "MPI Version:  " << MPI_VERSION << "." << MPI_SUBVERSION << std::endl;
#endif
        ss << "Config. Time: " << CONFIG_TIME << std::endl;
        ss << "Git Commit:   " << GIT_COMMIT_HASH << std::endl; 
        // Defining and parsing program options
        cxxopts::Options options(argv[0], ss.str());
        options.add_options()
                ("f,file", "Kernel file name", cxxopts::value<std::string>())
                ("n", "Number of repetitions",
                cxxopts::value<uint>()->default_value(std::to_string(DEFAULT_REPETITIONS)))
#ifdef INTEL_FPGA
                ("i", "Use memory Interleaving")
#endif
                ("skip-validation", "Skip the validation of the output data. This will speed up execution and helps when working with special data types.")
                ("device", "Index of the device that has to be used. If not given you "\
            "will be asked which device to use if there are multiple devices "\
            "available.", cxxopts::value<int>()->default_value(std::to_string(DEFAULT_DEVICE)))
                ("platform", "Index of the platform that has to be used. If not given "\
            "you will be asked which platform to use if there are multiple "\
            "platforms available.",
                cxxopts::value<int>()->default_value(std::to_string(DEFAULT_PLATFORM)))
                ("platform_str", "Name of the platform that has to be used", cxxopts::value<std::string>()->default_value(std::string()))
#ifdef NUM_REPLICATIONS
                ("r", "Number of used kernel replications",
                cxxopts::value<cl_uint>()->default_value(std::to_string(NUM_REPLICATIONS)))
#endif
#ifdef COMMUNICATION_TYPE_SUPPORT_ENABLED
                ("comm-type", "Used communication type for inter-FPGA communication",
                cxxopts::value<std::string>()->default_value(DEFAULT_COMM_TYPE))
#endif
                ("dump-json", "dump benchmark configuration and results to this file in json format", cxxopts::value<std::string>()->default_value(std::string("")))
                ("test", "Only test given configuration and skip execution and validation")
                ("h,help", "Print this help");


        addAdditionalParseOptions(options);

        try {

            cxxopts::ParseResult result = options.parse(argc, argv);

            if (result.count("h")) {
                // Just print help when argument is given
                std::cout << options.help() << std::endl;
                exit(0);
            }

            // Check parsed options and handle special cases
            if (result.count("f") <= 0) {
                throw fpga_setup::FpgaSetupException("Mandatory option is missing! Use -h to show all available options. ERROR: Kernel file must be given with option -f!");
            }

            // Create program settings from program arguments
            std::unique_ptr<TSettings> sharedSettings(
                    new TSettings(result));
            return sharedSettings;
        }
        catch (const cxxopts::OptionException& e) {
            throw fpga_setup::FpgaSetupException("Input parameters could not be parsed! Use -h to show all available options. ERROR: " + std::string(e.what()));
        }

    }

    /**
    * Prints the used configuration to std out before starting the actual benchmark.
    *
    * @param executionSettings The program settings that are parsed from the command line
    *                            using parseProgramParameters and extended by the used OpenCL 
    *                            context, program and device
    */
    void 
    printFinalConfiguration() {
        std::cout << PROGRAM_DESCRIPTION;
#ifdef _USE_MPI_
        std::cout << "MPI Version:  " << MPI_VERSION << "." << MPI_SUBVERSION << std::endl;
#endif
        std::cout << "Config. Time: " << CONFIG_TIME << std::endl;
        std::cout << "Git Commit:   " << GIT_COMMIT_HASH << std::endl; 
        
        std::cout << std::endl;
        std::cout << "Summary:" << std::endl;
        std::cout << *executionSettings << std::endl;
    }
    
    std::map<std::string, std::vector<double>>
    getTimingsMap() {
        return timings;
    }
    
    void
    addTimings(std::string key, std::vector<double> value) {
        timings.emplace(key, value);
    }
    
    // override for special benchmarks like b_eff
    virtual json getTimingsJson() {
        json j;
        for (auto const &key: timings) {
            std::vector<json> timings_list;
            for (auto const &timing: key.second) {
                json j;
                j["unit"] = "s";
                j["value"] = timing;
                timings_list.push_back(j);
            }
            j[key.first] = timings_list;
        }
        return j;
    }

    std::map<std::string, json> getResultsJson() {
        std::map<std::string, json> results_string;
        for (auto const &result: results) {
            json j;
            j["unit"] = result.second.unit;
            j["value"] = result.second.value;
            results_string[result.first] = j;
        }
        return results_string;
    }

    std::map<std::string, json> getErrorsJson() {
        std::map<std::string, json> errors_string; 
        for (auto const &error: errors) {
            json j;
            j["unit"] = error.second.unit;
            j["value"] = error.second.value;
            errors_string[error.first] = j;
        }
        return errors_string;
    }
    
    std::map<std::string, std::string>
    getEnvironmentMap() {
        std::map<std::string, std::string> env; 
        env["LD_LIBRARY_PATH"] = std::string(std::getenv("LD_LIBRARY_PATH"));
        return env;
    }

    json
    parseFPGATorusString(std::string str) {
        json j; 
        size_t space = str.find(" "); 
        std::string p_str = str.substr(0, space);
        std::string q_str = str.substr(space, str.size());
        j["P"] = stoi(p_str.substr(p_str.find("=") + 1, p_str.find(",")));
        j["Q"] = stoi(q_str.substr(q_str.find("=") + 1, q_str.size()));
        return j;
    }
    
    std::string
    getCurrentTime() {
        time_t time = std::time(0);
        const tm *local_time = std::localtime(&time);
        std::ostringstream oss;
        oss << std::put_time(local_time, "%a %b %d %T %Z %Y");
        return oss.str();
    }

    std::map<std::string, json>
    jsonifySettingsMap(std::map<std::string, std::string> settings_map) {
        json j;
        for (const auto& item: settings_map) {
            std::string key = item.first;
            std::string value = item.second;
            try {
                int value_int = stoi(value); 
                j[key] = value_int;
            } catch (std::invalid_argument const &ex) {
                if (key == "FPGA Torus") {
                    j[key] = parseFPGATorusString(value);
                } else if (key == "Emulate" || key == "Test Mode" || key == "Memory Interleaving" || key == "Replicate Inputs" || key == "Inverse" || key == "Diagonally Dominant" || "Dist. Buffers") {
                    j[key] = value == "Yes";
                } else {
                    j[key] = value; 
                }
            }     
        }
        return j;
    }
    
    void
    dumpConfigurationAndResults(std::string file_path) {
        std::fstream fs;
        fs.open(file_path, std::ios_base::out);
        if (!fs.is_open()) {
            std::cout << "Unable to open file for dumping configuration and results" << std::endl;
        } else {
            json dump;
            dump["name"] = PROGRAM_NAME;
#ifdef _USE_MPI_
            dump["mpi"] ={{"version", MPI_VERSION}, {"subversion", MPI_SUBVERSION}};
#endif
            dump["config_time"] = CONFIG_TIME;
            dump["execution_time"] = getCurrentTime();
            dump["git_commit"] = GIT_COMMIT_HASH;
            dump["version"] = VERSION;
            dump["device"] = executionSettings->getDeviceName();
            dump["settings"] = jsonifySettingsMap(executionSettings->programSettings->getSettingsMap());
            dump["timings"] = getTimingsJson();
            dump["results"] = getResultsJson();
            dump["errors"] = getErrorsJson();
            dump["validated"] = validated;
            dump["environment"] = getEnvironmentMap();

            fs << dump;
        }
    }

    /**
     * @brief Selects and prepares the target device and prints the final configuration.
     *          This method will initialize the executionSettings that are needed for the 
     *          benchmark execution.
     *          Thus, it has to be called before executeBenchmark() or executeKernel() method are called!
     * 
     * @param argc Number of input parameters as it is provided by the main function
     * @param argv Strings containing the input parameters as provided by the main function
     * 
     * @return true if setup was successful, false otherwise
     */
    bool 
    setupBenchmark(const int argc, char const* const* argv) {
        bool success = true;
        // Create deep copies of the input parameters to prevent modification from 
        // the cxxopts library
        int tmp_argc = argc;
        auto tmp_argv = new char*[argc + 1];
        auto tmp_pointer_storage = new char*[argc + 1];
        for (int i =0; i < argc; i++) {
            size_t len = strlen(argv[i]) + 1l;
            tmp_argv[i] = new char[len];
            tmp_pointer_storage[i] = tmp_argv[i];
            strcpy(tmp_argv[i], argv[i]);
        }
        tmp_argv[argc] = nullptr;

        try {

            std::unique_ptr<TSettings> programSettings = parseProgramParameters(tmp_argc, tmp_argv);

            std::unique_ptr<cl::Context> context;
            std::unique_ptr<cl::Program> program;
            std::unique_ptr<cl::Device> usedDevice;

            if (!programSettings->testOnly) {
                usedDevice = fpga_setup::selectFPGADevice(programSettings->defaultPlatform,
                                                                    programSettings->defaultDevice,
                                                                    programSettings->platformString);

                context = std::unique_ptr<cl::Context>(new cl::Context(*usedDevice));
                program = fpga_setup::fpgaSetup(context.get(), {*usedDevice},
                                                                    &programSettings->kernelFileName);
            }

            executionSettings = std::unique_ptr<ExecutionSettings<TSettings>>(new ExecutionSettings<TSettings>(std::move(programSettings), std::move(usedDevice), 
                                                                    std::move(context), std::move(program)));
            if (mpi_comm_rank == 0) {
                if (!checkInputParameters()) {
                    std::cerr << "ERROR: Input parameter check failed!" << std::endl;
                    throw std::runtime_error("Input parameter check failed!");
                }
                printFinalConfiguration();
            }
        }
        catch (std::exception& e) {
            std::cerr << "An error occured while setting up the benchmark: " << std::endl;
            std::cerr << "\t" << e.what() << std::endl;
            success = false;
        }

        for (int i =0; i < argc; i++) {
            delete [] tmp_pointer_storage[i];
        }
        delete [] tmp_argv;
        delete [] tmp_pointer_storage;

        benchmark_setup_succeeded = success;
        return success;

    }

    /**
     * @brief Execute the benchmark. This includes the initialization of the 
     *          input data, exectuon of the kernel, validation and printing the result
     * 
     * @return true If the validation is a success
     * @return false If the validation fails or an execution error occured
     */
    bool
    executeBenchmark() {

        if (!benchmark_setup_succeeded) {
            std::cerr << "Benchmark execution started without successfully running the benchmark setup!" << std::endl;
            return false;
        }
        if (executionSettings->programSettings->testOnly) {
            if (mpi_comm_rank == 0) {
                std::cout << "TEST MODE ENABLED: SKIP DATA GENERATION, EXECUTION, AND VALIDATION!" << std::endl;
                std::cout << "SUCCESSFULLY parsed input parameters!" << std::endl;
            }
            return benchmark_setup_succeeded;
        }
        if (mpi_comm_rank == 0) {
            std::cout << HLINE << "Start benchmark using the given configuration. Generating data..." << std::endl
                    << HLINE;
        }
       try {
            auto gen_start = std::chrono::high_resolution_clock::now();
            std::unique_ptr<TData> data = generateInputData();
            std::chrono::duration<double> gen_time = std::chrono::high_resolution_clock::now() - gen_start;
            
#ifdef _USE_MPI_
            MPI_Barrier(MPI_COMM_WORLD);
#endif

            if (mpi_comm_rank == 0) {
                std::cout << "Generation Time: " << gen_time.count() << " s"  << std::endl;
                std::cout << HLINE << "Execute benchmark kernel..." << std::endl
                        << HLINE;
            }

            auto exe_start = std::chrono::high_resolution_clock::now();
            executeKernel(*data);

#ifdef _USE_MPI_
        MPI_Barrier(MPI_COMM_WORLD);
#endif

            std::chrono::duration<double> exe_time = std::chrono::high_resolution_clock::now() - exe_start;

            if (mpi_comm_rank == 0) {
                std::cout << "Execution Time: " << exe_time.count() << " s"  << std::endl;
                std::cout << HLINE << "Validate output..." << std::endl
                        << HLINE;
            }

            if (!executionSettings->programSettings->skipValidation) {
                auto eval_start = std::chrono::high_resolution_clock::now();
                validated = validateOutput(*data);
                if (mpi_comm_rank == 0) {
                    printError();
                }
                std::chrono::duration<double> eval_time = std::chrono::high_resolution_clock::now() - eval_start;

                if (mpi_comm_rank == 0) {
                    std::cout << "Validation Time: " << eval_time.count() << " s" << std::endl;
                }
            }
            std::cout << HLINE << "Collect results..." << std::endl << HLINE;
            collectResults();
            
            if (mpi_comm_rank == 0) {
                if (executionSettings->programSettings->dumpfilePath.size() > 0) {
                    dumpConfigurationAndResults(executionSettings->programSettings->dumpfilePath);
                }
            
                printResults();

                if (!validated) {
                    std::cerr << HLINE << "ERROR: VALIDATION OF OUTPUT DATA FAILED!" << std::endl;
                }
                else {
                    std::cout << HLINE << "Validation: SUCCESS!" << std::endl;
                }
            }

            return validated;
       }
       catch (const std::exception& e) {
            std::cerr << "An error occured while executing the benchmark: " << std::endl;
            std::cerr << "\t" << e.what() << std::endl;
            return false;
       }
    }

    /**
     * @brief Get the Execution Settings object for further modifications (Mainly for testing purposes)
     * 
     * @return ExecutionSettings& The execution settings object
     */
    ExecutionSettings<TSettings>& getExecutionSettings() {
        return *executionSettings;
    }

    HpccFpgaBenchmark(int argc, char* argv[]) {
#ifdef _USE_MPI_
        int isMpiInitialized;
        MPI_Initialized(&isMpiInitialized);
        if (!isMpiInitialized) {
            MPI_Init(&argc, &argv);
            mpi_external_init = isMpiInitialized;
        }
        MPI_Comm_rank(MPI_COMM_WORLD, &mpi_comm_rank);
        MPI_Comm_size(MPI_COMM_WORLD, &mpi_comm_size);
#endif
        fpga_setup::setupEnvironmentAndClocks();
    }

    HpccFpgaBenchmark() {
#ifdef _USE_MPI_
        int isMpiInitialized;
        MPI_Initialized(&isMpiInitialized);
        mpi_external_init = isMpiInitialized;
        if (!isMpiInitialized) {
            std::cerr << "MPI needs to be initialized before constructing benchmark object or program parameters have to be given to the constructor!" << std::endl;
            throw std::runtime_error("MPI not initialized");
        }
        MPI_Comm_rank(MPI_COMM_WORLD, &mpi_comm_rank);
        MPI_Comm_size(MPI_COMM_WORLD, &mpi_comm_size);
#endif
        fpga_setup::setupEnvironmentAndClocks();        
    }

    virtual ~HpccFpgaBenchmark() {
#ifdef _USE_MPI_
        if (!mpi_external_init) {
            int isMpiFinalized;
            MPI_Finalized(&isMpiFinalized);
            if (!isMpiFinalized) {
                MPI_Finalize();
            }
        }
#endif        
    }

};


/**
 * @brief Prints the execution settings to an output stream
 * 
 * @tparam TSettings The program settings class used to create the execution settings
 * @param os The output stream
 * @param printedExecutionSettings The execution settings that have to be printed to the stream
 * @return std::ostream& The output stream after the execution settings are piped in
 */
template <class TSettings>
std::ostream& operator<<(std::ostream& os, ExecutionSettings<TSettings> const& printedExecutionSettings){
        std::string device_name = printedExecutionSettings.getDeviceName();
        os << std::left;
        for (auto k : printedExecutionSettings.programSettings->getSettingsMap()) {
            os   << std::setw(2 * ENTRY_SPACE) << k.first << k.second << std::endl;
        }
        os  << std::setw(2 * ENTRY_SPACE) << "Device"  << device_name << std::endl;
        os << std::right;
        return os;
}

} // namespace hpcc_base

#endif
