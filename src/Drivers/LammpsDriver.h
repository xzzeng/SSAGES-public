/**
 * This file is part of
 * SSAGES - Suite for Advanced Generalized Ensemble Simulations
 *
 * Copyright 2016 Ben Sikora <bsikora906@gmail.com>
 *                Hythem Sidky <hsidky@nd.edu>
 *
 * SSAGES is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * SSAGES is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with SSAGES.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include "lammps.h"
#include "../Observers/JSONObserver.h"
#include "Drivers/Driver.h"
#include "Drivers/DriverException.h"
#include "../Validator/ObjectRequirement.h"
#include "schema.h"
#include "input.h"
#include "modify.h"
#include "output.h"
#include "update.h"
#include "fix.h"

namespace SSAGES
{
	//! Driver for LAMMPS simulations
	class LammpsDriver : public Driver 
	{
	private:

		//! Pointer to the local instance of lammps
		std::shared_ptr<LAMMPS_NS::LAMMPS> _lammps;

		//! The lammps logfile
		std::string _logfile;

	public:

		//! Constructor
		/*!
		 * \param world_comm MPI global communicator.
		 * \param local_comm MPI local communicator.
		 * \param walkerID ID of the walker assigned to this driver.
		 */
		LammpsDriver(boost::mpi::communicator& world_comm,
					 boost::mpi::communicator& local_comm,
					 int walkerID) : 
		Driver(world_comm, local_comm, walkerID), _lammps(), _logfile() 
		{
		};

		//! Run simulation
		virtual void Run() override
		{
			std::string rline = "run " + std::to_string(iterations_);
			_lammps->input->one(rline.c_str());
		}

		//! Run LAMMPS input file
		/*!
		 * \param contents Content of the LAMMPS input file.
		 *
		 * This function exectures the contents of the given LAMMPS input file
		 * line by line and gathers the fix/hook.
		 */
		virtual void ExecuteInputFile(std::string contents) override
		{
			// Go through lammps.
			std::uniform_int_distribution<> dis(1, 999999);

			std::string token;
			std::istringstream ss(contents);
			bool reading_restart = false;

			if(_restartname != "none" && _readrestart)
			{
				_lammps->input->one(("read_restart " + _restartname + " remap").c_str());
				while(std::getline(ss, token, '\n'))
					if(token.find("#RESTART") != std::string::npos)
						reading_restart = true;

			}

			// need these 2 lines
			ss.clear(); // clear the `failbit` and `eofbit`
			ss.seekg(0); // rewind

			while(std::getline(ss, token, '\n'))
			{
				if(token.find("#RESTART") != std::string::npos)
					reading_restart = false;

				if(_readrestart && reading_restart)
					continue;

				_lammps->input->one(token.c_str());
			}

			// Initialize and create the restart parameters
			for(auto* o : _observers)
			{
				if(o->GetName() == "JSON")
				{
					_readrestart = true;
					JSONObserver* obs = static_cast<JSONObserver*>(o);
					std::string filename1 = obs->GetPrefix() + "_" + std::to_string(_wid) + ".restart";
					std::string filename2 = obs->GetPrefix() + "_" + std::to_string(_wid) + "b.restart";
					_lammps->input->one(("restart  " + std::to_string(obs->GetFrequency()) + " " + filename1 + " " + filename2).c_str());
				}
			}

			auto fid = _lammps->modify->find_fix("ssages");
			if(fid < 0)
				throw BuildException({"Could not find ssages fix in given input file!"});

			if(!(_hook = dynamic_cast<Hook*>(_lammps->modify->fix[fid])))
			{
				throw BuildException({"Unable to dynamic cast hook on node " + std::to_string(_world.rank())});			
			}
		}
		
		//! Set up the driver
		/*!
		 * \param json JSON value containing input information.
		 * \param path Path for JSON path specification.
		 */
		virtual void BuildDriver(const Json::Value& json, const std::string& path) override
		{

			Json::Value schema;
			Json::ObjectRequirement validator;
			Json::Reader reader;

			reader.parse(JsonSchema::LAMMPSDriver, schema);
			validator.Parse(schema, path);

			// Validate inputs.
			validator.Validate(json, path);
			if(validator.HasErrors())
				throw BuildException(validator.GetErrors());

			iterations_ = json.get("MDSteps",1).asInt();
			_inputfile = json.get("inputfile","none").asString();
			_restartname = json.get("restart file", "none").asString();
			_readrestart = json.get("read restart", false).asBool();

			if(_readrestart && _restartname == "none")
				throw BuildException({"You want to run from a restart but no file name provided (see 'restart file' in LAMMPS's schema for more informationz)"});
			
			// Silence of the lammps.
			char **largs = (char**) malloc(sizeof(char*) * 1);
			largs[0] = (char*) malloc(sizeof(char) * 1024);
			sprintf(largs[0], " ");
			_lammps = std::make_shared<LAMMPS_NS::LAMMPS>(1, largs, MPI_Comm(_comm));

			// Free.
			for(int i = 0; i < 1; ++i)
				free(largs[i]);
			free(largs);
		}

		virtual void Serialize(Json::Value& json) const override
		{
			// Call parent first.
			Driver::Serialize(json);

			json["MDSteps"] = iterations_;
			json["logfile"] = _logfile;
			json["type"] = "LAMMPS";
			//if true on first file
			if(_lammps->output->restart_toggle)
			{
				json["restart file"] = std::string(_lammps->output->restart2a);
			}
			else
			{
				json["restart file"] = std::string(_lammps->output->restart2b);
			}
		}
	};
}
