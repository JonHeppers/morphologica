/*
 * A wrapper around JSON code for saving and retrieving parameters
 */
#ifndef _CONFIG_H_
#define _CONFIG_H_

#include <json/json.h>
#include <fstream>
using std::ifstream;
using std::ofstream;
using std::ios;
#include <sstream>
using std::stringstream;
#include <string>
using std::string;
#include <iostream>
using std::cout;
using std::cerr;
using std::endl;
#include <stdexcept>
using std::exception;
using std::runtime_error;

#include "Process.h"

namespace morph {

    /*!
     * Callbacks class extends ProcessCallbacks
     */
    class ConfigProcessCallbacks : public ProcessCallbacks
    {
    public:
        ConfigProcessCallbacks (ProcessData* p) {
            this->parent = p;
        }
        void startedSignal (std::string msg) {}
        void errorSignal (int err) {
            this->parent->setErrorNum (err);
        }
        void processFinishedSignal (std::string msg) {
            this->parent->setProcessFinishedMsg (msg);
        }
        void readyReadStandardOutputSignal (void) {
            this->parent->setStdOutReady (true);
        }
        void readyReadStandardErrorSignal (void) {
            this->parent->setStdErrReady (true);
        }
    private:
        ProcessData* parent;
    };

    /*!
     * A configuration file class to help read simulation parameters from a JSON file.
     *
     * This reads a config file which should be arranged as a JSON file. The format is fairly
     * free-form; getters and setters are used to access the parameters stored in the config file.
     *
     * This class also provides code for updating the JSON config and writing out the updated config
     * into the log directory to make a record of the parameters used to generate a set of
     * simulation data.
     */
    class Config {

    public:
        //! Default constructor, when config should be a class member. Call init() before use.
        Config() : ready(false) {}

        //! Constructor which takes the path to the file that contains the JSON.
        Config (const string& configfile) {
            this->init (configfile);
        }

        //! Perform config file initialization.
        void init (const string& configfile) {
            stringstream ess;
            // Test for existence of the JSON file.
            ifstream jsonfile_test;
            int srtn = system ("pwd");
            if (srtn) {
                ess << "system call returned " << srtn;
                this->emsg = ess.str();
                this->ready = false;
                return;
            }
            jsonfile_test.open (configfile, ios::in);
            if (jsonfile_test.is_open()) {
                // Good, file exists.
                jsonfile_test.close();
            } else {
                ess << "json config file " << configfile << " not found.";
                this->emsg = ess.str();
                this->ready = false;
                return;
            }

            // Parse the JSON
            ifstream jsonfile (configfile, ifstream::binary);
            string errs;
            Json::CharReaderBuilder rbuilder;
            rbuilder["collectComments"] = false;
            bool parsingSuccessful = Json::parseFromStream (rbuilder, jsonfile, &this->root, &errs);
            if (!parsingSuccessful) {
                // report to the user the failure and their locations in the document.
                ess << "Failed to parse JSON: " << errs;
                this->emsg = ess.str();
                this->ready = false;
                return;
            }
            // JSON is open and parsed
            this->ready = true;
        }

        /*!
         * Launch git sub-processes to determine info about the
         * current repository. Intended for use with code that will
         * save a Json formatted log of a simulation run.
         *
         * @codedir The name of the directory in which significant
         * code is located. If git status detects changes in this
         * directory, then information to this effect will be inserted
         * into this->root.
         */
        void insertGitInfo (const string& codedir) {
            ProcessData pD;
            ConfigProcessCallbacks cb(&pD);
            Process p;
            string command ("/usr/bin/git");

            list<string> args1;
            args1.push_back ("git");
            args1.push_back ("rev-parse");
            args1.push_back ("HEAD");

            try {
                p.setCallbacks (&cb);
                p.start (command, args1);
                p.probeProcess ();
                if (!p.waitForStarted()) {
                    throw runtime_error ("Process failed to start");
                }
                while (p.running() == true) {
                    p.probeProcess();
                }

                stringstream theOutput;
                theOutput << p.readAllStandardOutput();
                string line = "";
                int nlines = 0;
                while (getline (theOutput, line, '\n')) {
                    cout << "Current git HEAD: " << line << endl;
                    if (nlines++ > 0) {
                        break;
                    }
                    this->root["git_head"] = line; // Should be one line only
                }

            } catch (const exception& e) {
                cerr << "Exception: " << e.what() << endl;
                this->root["git_head"] = "unknown";
            }

            // Reset Process with arg true to keep callbacks
            p.reset (true);

            list<string> args2;
            args2.push_back ("git");
            args2.push_back ("status");

            try {
                p.start (command, args2);
                p.probeProcess ();
                if (!p.waitForStarted()) {
                    throw runtime_error ("Process failed to start");
                }
                while (p.running() == true) {
                    p.probeProcess();
                }

                stringstream theOutput;
                theOutput << p.readAllStandardOutput();
                string line = "";
                bool lm = false;
                bool ut = false;
                while (getline (theOutput, line, '\n')) {
                    if (line.find("modified:") != string::npos) {
                        if (line.find(codedir) != string::npos) {
                            if (!lm) {
                                this->root["git_modified_sim"] = true;
                                cout << "Repository has local modifications in " << codedir << " dir" << endl;
                            }
                            lm = true;
                        }
                    }
                    if (line.find("Untracked files:") != string::npos) {
                        if (line.find(codedir) != string::npos) {
                            if (!ut) {
                                this->root["git_untracked_sim"] = true;
                                cout << "Repository has untracked files present in " << codedir << " dir" << endl;
                            }
                            ut = true;
                        }
                    }
                }

            } catch (const exception& e) {
                stringstream ee;
                ee << "Exception: " << e.what();
                this->emsg = ee.str();
                this->root["git_status"] = "unknown";
            }

            // Reset for third call
            p.reset (true);

            // This gets the git branch name
            list<string> args3;
            args3.push_back ("git");
            args3.push_back ("rev-parse");
            args3.push_back ("--abbrev-ref");
            args3.push_back ("HEAD");

            try {
                p.start (command, args3);
                p.probeProcess ();
                if (!p.waitForStarted()) {
                    throw runtime_error ("Process failed to start");
                }
                while (p.running() == true) {
                    p.probeProcess();
                }

                stringstream theOutput;
                theOutput << p.readAllStandardOutput();
                string line = "";
                int nlines = 0;
                while (getline (theOutput, line, '\n')) {
                    cout << "Current git branch: " << line << endl;
                    if (nlines++ > 0) {
                        break;
                    }
                    this->root["git_branch"] = line; // Should be one line only
                }

            } catch (const exception& e) {
                stringstream ee;
                ee << "Exception: " << e.what();
                this->emsg = ee.str();
                this->root["git_branch"] = "unknown";
            }
        }

        //! Write out the JSON to file.
        void write (const string& outfile) {
            ofstream configout;
            configout.open (outfile.c_str(), ios::out|ios::trunc);
            if (configout.is_open()) {
                configout << this->root;
                configout.close();
            } else {
                this->emsg = "Failed to open file '" + outfile + "' for writing";
                this->ready = false;
            }
        }

        //! Wrappers around gets
        //@{
        bool getBool (const string& thing, bool defaultval) {
            return this->root.get (thing, defaultval).asBool();
        }
        int getInt (const string& thing, int defaultval) {
            return this->root.get (thing, defaultval).asInt();
        }
        unsigned int getUInt (const string& thing, unsigned int defaultval) {
            return this->root.get (thing, defaultval).asUInt();
        }
        float getFloat (const string& thing, float defaultval) {
            return this->root.get (thing, defaultval).asFloat();
        }
        double getDouble (const string& thing, double defaultval) {
            return this->root.get (thing, defaultval).asDouble();
        }
        string getString (const string& thing, const string& defaultval) {
            return this->root.get (thing, defaultval).asString();
        }
        Json::Value getArray (const string& arrayname) {
            return this->root[arrayname];
        }
        //@}

        //! Setters
        //@{
        void set (const string& thing, bool value) {
            this->root[thing] = value;
        }
        void set (const string& thing, int value) {
            this->root[thing] = value;
        }
        void set (const string& thing, unsigned int value) {
            this->root[thing] = value;
        }
        void set (const string& thing, float value) {
            this->root[thing] = value;
        }
        void set (const string& thing, double value) {
            this->root[thing] = value;
        }
        void set (const string& thing, const string& value) {
            this->root[thing] = value;
        }
        //@}

        //! Set true when config object ready to be used
        bool ready = false;

        //! If !ready, error message is here.
        string emsg = "";

        // The root object which is set up in the constructor
        Json::Value root;
    };
} // namespace

#endif // _CONFIG_H_
