/*
  This file is part of MADNESS.

  Copyright (C) <2007> <Oak Ridge National Laboratory>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA

  For more information please contact:

  Robert J. Harrison
  Oak Ridge National Laboratory
  One Bethel Valley Road
  P.O. Box 2008, MS-6367

  email: harrisonrj@ornl.gov
  tel:   865-241-3937
  fax:   865-572-0680


  $Id: test.cc 257 2007-06-25 19:09:38Z HartmanBaker $
*/

/// \file mraplot.cc
/// \brief Function plotting utility

#define WORLD_INSTANTIATE_STATIC_TEMPLATES
#include <mra/mra.h>
#include <unistd.h>
#include <cstdio>
#include <constants.h>
#include <misc/phandler.h>

using namespace madness;

template <typename T, int NDIM>
struct lbcost {
    double operator()(const Key<NDIM>& key, const FunctionNode<T,NDIM>& node) const {
        return 1.0;
    }
};

std::string help = "\n\
      Input is read from standard input.\n\
\n\
      Keywords may appear in any order until the plot keyword is  \n\
      detected, at which point the plot is generated.  Input processing  \n\
      then resumes with previous input values being remembered. \n\
 \n\
      Thus, multiple plots may be generated by one input file. \n\
      If only one plot is being generated, the plot keyword may \n\
      be omitted (plot is triggered by hitting EOF). \n\
 \n\
      !! If the parallel archive holding the function was generated \n\
      !! with multiple writers you presently must run mraplot in parallel \n\
      !! with at least as many MPI processes. \n\
 \n\
      REQUIRED KEYWORDS \n\
      .   input <string filename> // Input file name ... no default for this! \n\
 \n\
      OPTIONAL KEYWORDS \n\
      .   output <string filename> // Default is 'plot' \n\
      .   ndim <int ndim>   // No. of dimensions ... default is 3 \n\
      .   cell <double lo> <double hi> [...] // Compute cell volume ... default is [0,1]^ndim \n\
      .   ascii             // Text output for volume data [default is binary] \n\
      .   text              // Text output for volume data [default is binary] \n\
      .   dx                // Specifies DX format for volume data [default is dx] \n\
      .   vtk <str function_name> // Specifies VTK format for volume data [default is dx], giving the function name function_name \n\
      .   real              // Sets data type to real, default is real \n\
      .   complex           // Sets data type to complex, default is real \n\
      .   line              // Sets plot type to line, default is volume \n\
      .   volume            // Sets plot type to volume, default is volume \n\
      .   plot_cell <double lo> <double hi> [...] // Plot range in each dimension, default is compute cell \n\
      .   npt               // No. of points in each dimension (default is 101) \n\
      .   formula           // Also plot analytic expression \n\
      .   exit              // exits the program gracefully \n\
      .   quit              // exits the program gracefully \n\
 \n\
      EXAMPLE \n\
      .   For a real function in parallel archive 'psi_step22' it  \n\
      .   makes a volume plot over the whole domain and then a line \n\
      .   plot along the z axis between [-10,10] \n\
      .   \n\
      .   cell -100 100 -100 100 -100 100 \n\
      .   input psi_step22 \n\
      .   output psi22.dx \n\
      .   plot \n\
      . \n\
      .   vtk my_function \n\
      .   output psi22.vts \n\
      .   plot \n\
      . \n\
      . \n\
      .   dx \n\
      .   plot_cell 0 0 0 0 -10 10 \n\
      .   output psi22.dat \n\
      .   line \n\
      .   plot \n\
      ";

class Plotter {

public:

    World& world;
    Tensor<double> cell;        // Computational cell
    Tensor<double> plot_cell;   // Plotting volume
    std::string data_type;           // presently only double or complex
    std::string plot_type;           // line or volume
    std::string input_filename;      // filename for function on disk
    std::string output_filename;     // output file name for plot data
    std::string output_format;       // output format for volume data (presently only dx)
    std::string formula;             // analytic function to plot
    std::string function_name;       // function name for VTK output
    std::vector<long> npt;           // no. points in each dimension
    int ndim;                   // no. of dimensions
    bool binary;                // output format for plot data
    bool finished;              // true if finishing
    
    template <typename Archive>
    void serialize(Archive& ar) {
        ar & cell & plot_cell & data_type & plot_type
            & input_filename & output_filename & output_format & function_name
            & npt & ndim & binary & finished;
    }

    Plotter(World& world) 
        : world(world)
        , cell()
        , plot_cell()
        , data_type("double")
        , plot_type("volume")
        , input_filename()
        , output_filename("plot")
        , output_format("dx")
        , formula("")
        , function_name("function")
        , ndim(3)
        , binary(true)
        , finished(true)
    {}

    std::string read_to_end_of_line(std::istream& input) {
        std::string buf;
        while (1) {
            int c = input.get();
            if (c == '\n') break;
            buf.append(1, c);
        }
        return buf;
    }

    std::vector<long> read_npt(std::istream& input) {
        std::istringstream s(read_to_end_of_line(input));
        std::vector<long> npt;
        int i;
        while (s >> i) {
            npt.push_back(i);
        }
        return npt;
    }

    // Read pairs of floating point values and return appropriately sized tensor
    Tensor<double> read_cell(std::istream& input) {
        std::istringstream s(read_to_end_of_line(input));
        double v[65];
        int n = 0;
        while (s >> v[n]) {
            n++;
            MADNESS_ASSERT(n < 65);
        }
        // There should be an even number
        int ndim = n/2;
        MADNESS_ASSERT(n==ndim*2 && ndim>=1);
        Tensor<double> cell(ndim,2);
        for (int i=0; i<ndim; i++) {
            cell(i,0) = v[2*i  ];
            cell(i,1) = v[2*i+1];
        }
        return cell;
    }

    void read(std::istream& input) {
        finished = true;
        std::string token;
        while (input >> token) {
            finished = false;

            if (token == "ndim") {
                input >> ndim;
            }
            else if (token == "ascii") {
                binary = false;
            }
            else if (token == "text") {
                binary = false;
            }
            else if (token == "dx") {
                output_format = "dx";
            }
            else if (token == "vtk") {
                output_format = "vtk";

                // get the name of the function
                if(!(input >> function_name))
                    MADNESS_EXCEPTION("VTK format requires a function name", 0);
            }
            else if (token == "input") {
                input >> input_filename;
            }
            else if (token == "real") {
                data_type = "real";
            }
            else if (token == "complex") {
                data_type = "complex";
            }
            else if (token == "line") {
                plot_type = "line";
            }
            else if (token == "volume") {
                plot_type = "volume";
            }
            else if (token == "cell") {
                cell = read_cell(input);
            }
            else if (token == "plot_cell") {
                plot_cell = read_cell(input);
            }
            else if (token == "npt") {
                npt = read_npt(input);
            }
            else if (token == "plot") {
                break;
            }
            else if (token == "input") {
                input >> input_filename;
            }
            else if (token == "output") {
                input >> output_filename;
            }
            else if (token == "formula") {
                input >> formula;
            }
            else if (token == "quit" || token == "exit") {
                finished = true;
                break;
            }
            else {
                print("unknown keyword =", token);
                MADNESS_EXCEPTION("unknown plotter keyword", 0);
            }
        }

        if (finished) return;
        
        // Implement runtime defaults
        if (cell.size <= 0) {
            MADNESS_ASSERT(ndim>0 && ndim<=6);
            cell = Tensor<double>(ndim,2);
            for (int i=0; i<ndim; i++) cell(i,1) = 1.0;
        }
        if (plot_cell.size <= 0) plot_cell = copy(cell);
        while (int(npt.size()) < ndim) npt.push_back(101);

        // Warm and fuzzy
        std::string ff[2] = {"text","binary"};
        print(plot_type,"plot of", data_type, "function in", ndim,"dimensions from file", input_filename, "to", ff[binary], "file", output_filename);
        print("  compute cell");
        print(cell);
        print("  plot cell");
        print(plot_cell);
        print("  number of points");
        print(npt);
        print("");

        // Sanity check
        MADNESS_ASSERT(ndim>0 && ndim<=6);
        MADNESS_ASSERT(cell.dim[0]==ndim && cell.dim[1]==2);
        MADNESS_ASSERT(plot_cell.dim[0]==ndim && plot_cell.dim[1]==2);
    }

    template <typename T, int NDIM>
    void dolineplot(const Function<T,NDIM>& f) {
        Vector<double,NDIM> lo, hi;
        for (int i=0; i<NDIM; i++) {
            lo[i] = plot_cell(i,0);
            hi[i] = plot_cell(i,1);
        }
        
        plot_line(output_filename.c_str(), npt[0], lo, hi, f);
    }


    template <typename T, int NDIM>
    void dovolumeplot(const Function<T,NDIM>& f) {
        if(output_format == "dx") {
            plotdx(f, output_filename.c_str(), plot_cell, npt, binary);
        }
        else if(output_format == "vtk") {
            Vector<double, NDIM> plotlo, plothi;
            Vector<long, NDIM> numpt;
            for(int i = 0; i < NDIM; ++i) {
                plotlo[i] = plot_cell(i, 0);
                plothi[i] = plot_cell(i, 1);
                numpt[i] = npt[i];
            }
            
            plotvtk_begin(world, output_filename.c_str(), plotlo, plothi,
                numpt, binary);
            plotvtk_data(f, function_name.c_str(), world,
                output_filename.c_str(), plotlo, plothi, numpt, binary);
            plotvtk_end<NDIM>(world, output_filename.c_str(), binary);
        }
    }


    template <typename T, int NDIM>
    void dolineplot(const Function<T,NDIM>& f, const Function<T,NDIM>& g) {
        Vector<double,NDIM> lo, hi;
        for (int i=0; i<NDIM; i++) {
            lo[i] = plot_cell(i,0);
            hi[i] = plot_cell(i,1);
        }
        
        plot_line(output_filename.c_str(), npt[0], lo, hi, f, g);
    }


    template <typename T, int NDIM>
    void dovolumeplot(const Function<T,NDIM>& f, const Function<T,NDIM>& g) {
        if(output_format == "dx") {
          MADNESS_EXCEPTION("plot type not supported with user functions!",0);
        }
        else if(output_format == "vtk") {
            Vector<double, NDIM> plotlo, plothi;
            Vector<long, NDIM> numpt;
            for(int i = 0; i < NDIM; ++i) {
                plotlo[i] = plot_cell(i, 0);
                plothi[i] = plot_cell(i, 1);
                numpt[i] = npt[i];
            }
            
            plotvtk_begin(world, output_filename.c_str(), plotlo, plothi,
                numpt, binary);
            plotvtk_data(f, function_name.c_str(), world,
                output_filename.c_str(), plotlo, plothi, numpt, binary);
            plotvtk_data(g, function_name.c_str(), world,
                output_filename.c_str(), plotlo, plothi, numpt, binary);
            plotvtk_end<NDIM>(world, output_filename.c_str(), binary);
        }
    }


    template <typename T, int NDIM>
    void doplot1() {

        // Set up environment for this dimension
        FunctionDefaults<NDIM>::set_cell(cell);

        // Load the function
        Function<T,NDIM> f;
        ParallelInputArchive ar(world, input_filename.c_str());
        ar & f;

        // Load the user's function
        if (formula != "") {
         typedef FunctionFactory<T,NDIM> factoryT;
         typedef SharedPtr< FunctionFunctorInterface<T, NDIM> > functorT;
         Function<T,NDIM> pFunc = factoryT(world).functor(functorT(
                        new ParserHandler<T,NDIM>("exp(-abs(r))")));

          if      (plot_type == "volume")    dovolumeplot<T,NDIM>(f,pFunc);
          else if (plot_type == "line")      dolineplot<T,NDIM>(f,pFunc);
          else    MADNESS_EXCEPTION("unknown plot type",0);
        } // end if formula present
        else {
          if      (plot_type == "volume")    dovolumeplot<T,NDIM>(f);
          else if (plot_type == "line")      dolineplot<T,NDIM>(f);
          else    MADNESS_EXCEPTION("unknown plot type",0);
        }
    }


    template <typename T>
    void doplot() {
        // Redirect to right dimension
        if      (ndim == 1) doplot1<T,1>();
        else if (ndim == 2) doplot1<T,2>();
        else if (ndim == 3) doplot1<T,3>();
        else if (ndim == 4) doplot1<T,4>();
//         else if (ndim == 5) doplot1<T,5>();
//         else if (ndim == 6) doplot1<T,6>();
        else                MADNESS_EXCEPTION("invalid ndim", ndim);
    }

    void plot() {
        // Redirect to right data type
        if (data_type == "double") {
            doplot<double>();
        }
        else if (data_type == "complex") {
            doplot< std::complex<double> >();
        }
        else {
            MADNESS_EXCEPTION("uknown data type",0);
        }
    }
};

int main(int argc, char**argv) {
    initialize(argc, argv);

    try {
        World world(MPI::COMM_WORLD);
        bool done = false;
        if (world.rank() == 0) {
            for (int i=0; i<argc; i++) {
                if (!strcmp(argv[i],"--help")) {
                    print(help);
                    done = true;
                }
            }
        }
        world.gop.broadcast(done);
        if (!done) {
            startup(world,argc,argv);
            if (world.rank() == 0) print(" ");

            Plotter plotter(world);
            while (1) {
                if (world.rank() == 0) {
                    plotter.read(std::cin);
                }
                world.gop.broadcast_serializable(plotter, 0);
                if (plotter.finished) break;
                plotter.plot();
            }
        }

        world.gop.fence();
    }
    catch (const MPI::Exception& e) {
        //        print(e);
        error("caught an MPI exception");
    }
    catch (const madness::MadnessException& e) {
        print(e);
        error("caught a MADNESS exception");
    }
    catch (const madness::TensorException& e) {
        print(e);
        error("caught a Tensor exception");
    }
    catch (const char* s) {
        print(s);
        error("caught a c-string exception");
    }
    catch (char* s) {
        print(s);
        error("caught a c-string exception");
    }
    catch (const std::string& s) {
        print(s);
        error("caught a string (class) exception");
    }
    catch (const std::exception& e) {
        print(e.what());
        error("caught an STL exception");
    }
    catch (...) {
        error("caught unhandled exception");
    }

    finalize();
    return 0;
}

