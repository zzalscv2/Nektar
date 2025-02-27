///////////////////////////////////////////////////////////////////////////////
//
// File LinearSWE.cpp
//
// For more information, please see: http://www.nektar.info
//
// The MIT License
//
// Copyright (c) 2006 Division of Applied Mathematics, Brown University (USA),
// Department of Aeronautics, Imperial College London (UK), and Scientific
// Computing and Imaging Institute, University of Utah (USA).
//
// License for the specific language governing rights and limitations under
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the "Software"),
// to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense,
// and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
// OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.
//
// Description: Linear Shallow water equations in primitive variables
//
///////////////////////////////////////////////////////////////////////////////

#include <iostream>
#include <iomanip>
#include <boost/algorithm/string.hpp>

#include <MultiRegions/AssemblyMap/AssemblyMapDG.h>
#include <ShallowWaterSolver/EquationSystems/LinearSWE.h>

namespace Nektar
{
  string LinearSWE::className = 
     SolverUtils::GetEquationSystemFactory().RegisterCreatorFunction(
	  "LinearSWE", LinearSWE::create, 
          "Linear shallow water equation in primitive variables.");
  
  LinearSWE::LinearSWE(
          const LibUtilities::SessionReaderSharedPtr& pSession)
    : ShallowWaterSystem(pSession)
  {
  }

  void LinearSWE::v_InitObject()
  {
      ShallowWaterSystem::v_InitObject();
      
    if (m_explicitAdvection)
      {
	m_ode.DefineOdeRhs     (&LinearSWE::DoOdeRhs,        this);
	m_ode.DefineProjection (&LinearSWE::DoOdeProjection, this);
      }
    else
      {
	ASSERTL0(false, "Implicit SWE not set up.");
      }

    // Type of advection class to be used
    switch(m_projectionType)
      {
	// Continuous field 
      case MultiRegions::eGalerkin:
	{
	  //  Do nothing 
	  break;
	}
	// Discontinuous field 
      case MultiRegions::eDiscontinuous:
	{
	  string advName;
	  string diffName;
	  string riemName;
          
	  //---------------------------------------------------------------
	  // Setting up advection and diffusion operators
	  // NB: diffusion not set up for SWE at the moment
	  //     but kept here for future use ...
	  m_session->LoadSolverInfo("AdvectionType", advName, "WeakDG");
	  // m_session->LoadSolverInfo("DiffusionType", diffName, "LDGEddy");
	  m_advection = SolverUtils::GetAdvectionFactory()
	    .CreateInstance(advName, advName);
	  // m_diffusion = SolverUtils::GetDiffusionFactory()
	  //                             .CreateInstance(diffName, diffName);
	  
	  m_advection->SetFluxVector(&LinearSWE::GetFluxVector, this);
	  // m_diffusion->SetFluxVectorNS(&ShallowWaterSystem::
	  //                                  GetEddyViscosityFluxVector, this); 
         
	  // Setting up Riemann solver for advection operator
	  m_session->LoadSolverInfo("UpwindType", riemName, "NoSolver");
	  if ((riemName == "LinearHLL") && (m_constantDepth != true))
	    {
	      ASSERTL0(false,"LinearHLL only valid for constant depth"); 
	    }
	  m_riemannSolver = SolverUtils::GetRiemannSolverFactory()
	    .CreateInstance(riemName);
         
       	  // Setting up upwind solver for diffusion operator
	  // m_riemannSolverLDG = SolverUtils::GetRiemannSolverFactory()
	  //                                 .CreateInstance("UpwindLDG");
	  
	  // Setting up parameters for advection operator Riemann solver 
	  m_riemannSolver->SetParam (
                                     "gravity",  
                                     &LinearSWE::GetGravity,   this);
      m_riemannSolver->SetAuxVec(
                                     "vecLocs",
                                     &LinearSWE::GetVecLocs,  this);
	  m_riemannSolver->SetVector(
				     "N",
				     &LinearSWE::GetNormals, this);
	
	  // The numerical flux for linear SWE requires depth information
	  int nTracePointsTot = m_fields[0]->GetTrace()->GetTotPoints();
	  m_dFwd = Array<OneD, NekDouble>(nTracePointsTot);
	  m_dBwd = Array<OneD, NekDouble>(nTracePointsTot);
	  m_fields[0]->GetFwdBwdTracePhys(m_depth, m_dFwd, m_dBwd);
	  CopyBoundaryTrace(m_dFwd,m_dBwd);
	  m_riemannSolver->SetScalar(
				     "depthFwd",
				     &LinearSWE::GetDepthFwd, this);
	  m_riemannSolver->SetScalar(
				     "depthBwd",
				     &LinearSWE::GetDepthBwd, this);

	  // Setting up parameters for diffusion operator Riemann solver
	  // m_riemannSolverLDG->AddParam (
	  //                     "gravity",  
	  //                     &NonlinearSWE::GetGravity,   this);
      // m_riemannSolverLDG->SetAuxVec(
      //                     "vecLocs",
      //                     &NonlinearSWE::GetVecLocs,  this);
	  // m_riemannSolverLDG->AddVector(
	  //                     "N",
	  //                     &NonlinearSWE::GetNormals, this);
          
	  // Concluding initialisation of advection / diffusion operators
	  m_advection->SetRiemannSolver   (m_riemannSolver);
	  //m_diffusion->SetRiemannSolver   (m_riemannSolverLDG);
	  m_advection->InitObject         (m_session, m_fields);
	  //m_diffusion->InitObject         (m_session, m_fields);
	  break;
	}
      default:
	{
	  ASSERTL0(false, "Unsupported projection type.");
	  break;
	}
      }
    

  }
  
  LinearSWE::~LinearSWE()
  {
    
  }
 
  // physarray contains the conservative variables
  void LinearSWE::AddCoriolis(const Array<OneD, const Array<OneD, NekDouble> > &physarray,
			      Array<OneD, Array<OneD, NekDouble> > &outarray)
  {
    
    int ncoeffs    = GetNcoeffs();
    int nq         = GetTotPoints();
    
    Array<OneD, NekDouble> tmp(nq);
    Array<OneD, NekDouble> mod(ncoeffs);
	
    switch(m_projectionType)
      {
      case MultiRegions::eDiscontinuous:
	{	  
	  // add to u equation
	  Vmath::Vmul(nq,m_coriolis,1,physarray[2],1,tmp,1);
	  m_fields[0]->IProductWRTBase(tmp,mod);
	  m_fields[0]->MultiplyByElmtInvMass(mod,mod);
	  m_fields[0]->BwdTrans(mod,tmp);
	  Vmath::Vadd(nq,tmp,1,outarray[1],1,outarray[1],1);
	   
	  // add to v equation
	  Vmath::Vmul(nq,m_coriolis,1,physarray[1],1,tmp,1);
	  Vmath::Neg(nq,tmp,1);
	  m_fields[0]->IProductWRTBase(tmp,mod);
	  m_fields[0]->MultiplyByElmtInvMass(mod,mod);
	  m_fields[0]->BwdTrans(mod,tmp);
	  Vmath::Vadd(nq,tmp,1,outarray[2],1,outarray[2],1);
	}
	break;
      case MultiRegions::eGalerkin:
      case MultiRegions::eMixed_CG_Discontinuous:
	{
	  // add to u equation
	  Vmath::Vmul(nq,m_coriolis,1,physarray[2],1,tmp,1);
	  Vmath::Vadd(nq,tmp,1,outarray[1],1,outarray[1],1);
	  
	  // add to v equation
	  Vmath::Vmul(nq,m_coriolis,1,physarray[1],1,tmp,1);
	  Vmath::Neg(nq,tmp,1);
	  Vmath::Vadd(nq,tmp,1,outarray[2],1,outarray[2],1);
	}
	break;
      default:
	ASSERTL0(false,"Unknown projection scheme for the NonlinearSWE");
	break;
      }


  }
  
  void LinearSWE::DoOdeRhs(const Array<OneD, const Array<OneD, NekDouble> >&inarray,  
			            Array<OneD,       Array<OneD, NekDouble> >&outarray, 
			      const NekDouble time) 
  {
    int i, j;
    int ndim       = m_spacedim;
    int nvariables = inarray.num_elements();
    int nq         = GetTotPoints();
  
    
    switch(m_projectionType)
      {
      case MultiRegions::eDiscontinuous:
	{
	 
	  //-------------------------------------------------------
	  // Compute the DG advection including the numerical flux
	  // by using SolverUtils/Advection 
	  // Input and output in physical space
	  Array<OneD, Array<OneD, NekDouble> > advVel;
	  
	  m_advection->Advect(nvariables, m_fields, advVel, inarray,
	                      outarray, time);
	  //-------------------------------------------------------
	  
	  
	  //-------------------------------------------------------
	  // negate the outarray since moving terms to the rhs
	  for(i = 0; i < nvariables; ++i)
	    {
	      Vmath::Neg(nq,outarray[i],1);
	    }
	  //-------------------------------------------------------
	  
	  
	  //-------------------------------------------------
	  // Add "source terms"
	  // Input and output in physical space
	  	  
	  // Coriolis forcing
	  if (m_coriolis.num_elements() != 0)
	    {
	      AddCoriolis(inarray,outarray);
	    }
	  //------------------------------------------------- 
	   
	}
	break;
      case MultiRegions::eGalerkin:
      case MultiRegions::eMixed_CG_Discontinuous:
	{
	
	  //-------------------------------------------------------
	  // Compute the fluxvector in physical space
	  Array<OneD, Array<OneD, Array<OneD, NekDouble> > > 
	    fluxvector(nvariables);
	  
	  for (i = 0; i < nvariables; ++i)
            {
	      fluxvector[i] = Array<OneD, Array<OneD, NekDouble> >(ndim);
	      for(j = 0; j < ndim; ++j)
                {
		  fluxvector[i][j] = Array<OneD, NekDouble>(nq);
                }
            }
	  
	  LinearSWE::GetFluxVector(inarray, fluxvector);
	  //-------------------------------------------------------

	  
	  //-------------------------------------------------------
	  // Take the derivative of the flux terms
	  // and negate the outarray since moving terms to the rhs
	  Array<OneD,NekDouble> tmp(nq);
	  Array<OneD, NekDouble>tmp1(nq);           
	  
	  for(i = 0; i < nvariables; ++i)
	    {
	      m_fields[i]->PhysDeriv(MultiRegions::DirCartesianMap[0],fluxvector[i][0],tmp);
	      m_fields[i]->PhysDeriv(MultiRegions::DirCartesianMap[1],fluxvector[i][1],tmp1);
	      Vmath::Vadd(nq,tmp,1,tmp1,1,outarray[i],1);
	      Vmath::Neg(nq,outarray[i],1);
	    }
	  
	  //-------------------------------------------------
	  // Add "source terms"
	  // Input and output in physical space
	  
	  // Coriolis forcing
	  if (m_coriolis.num_elements() != 0)
	    {
	      AddCoriolis(inarray,outarray);
	    }
	  //------------------------------------------------- 
	}
	break;
      default:
	ASSERTL0(false,"Unknown projection scheme for the NonlinearSWE");
	break;
      }
  }
 

  void LinearSWE::DoOdeProjection(const Array<OneD, const Array<OneD, NekDouble> >&inarray,
				           Array<OneD,       Array<OneD, NekDouble> >&outarray,
				     const NekDouble time)
  {
    int i;
    int nvariables = inarray.num_elements();
   
    
    switch(m_projectionType)
      {
      case MultiRegions::eDiscontinuous:
	{
	  
	  // Just copy over array
	  int npoints = GetNpoints();
	  
	  for(i = 0; i < nvariables; ++i)
	    {
	      Vmath::Vcopy(npoints, inarray[i], 1, outarray[i], 1);
	    }
	  SetBoundaryConditions(outarray, time);
	  break;
	}
      case MultiRegions::eGalerkin:
      case MultiRegions::eMixed_CG_Discontinuous:
	{
	  
	  EquationSystem::SetBoundaryConditions(time);
	  Array<OneD, NekDouble> coeffs(m_fields[0]->GetNcoeffs());
	  
	  for(i = 0; i < nvariables; ++i)
          {
              m_fields[i]->FwdTrans(inarray[i],coeffs);
	      m_fields[i]->BwdTrans_IterPerExp(coeffs,outarray[i]);
          }
	  break;
	}
      default:
	ASSERTL0(false,"Unknown projection scheme");
	break;
      }
  }
  

   //----------------------------------------------------
  void LinearSWE::SetBoundaryConditions(
    Array<OneD, Array<OneD, NekDouble> > &inarray,
    NekDouble time)
  { 
      std::string varName;
      int nvariables = m_fields.num_elements();
      int cnt = 0;

      // loop over Boundary Regions
      for(int n = 0; n < m_fields[0]->GetBndConditions().num_elements(); ++n)
      {	
          // Wall Boundary Condition
          if (boost::iequals(m_fields[0]->GetBndConditions()[n]->GetUserDefined(),"Wall"))
          {
              WallBoundary2D(n, cnt, inarray);
          }
	
          // Time Dependent Boundary Condition (specified in meshfile)
          if (m_fields[0]->GetBndConditions()[n]->IsTimeDependent())
          {
              for (int i = 0; i < nvariables; ++i)
              {
                  varName = m_session->GetVariable(i);
                  m_fields[i]->EvaluateBoundaryConditions(time, varName);
              }
          }
          cnt += m_fields[0]->GetBndCondExpansions()[n]->GetExpSize();
      }
  }
  
  //----------------------------------------------------
  /**
     * @brief Wall boundary condition.
     */
    void LinearSWE::WallBoundary(
        int                                   bcRegion,
        int                                   cnt, 
        Array<OneD, Array<OneD, NekDouble> > &physarray)
    { 
        int i;
        int nTracePts = GetTraceTotPoints();
        int nvariables      = physarray.num_elements();
        
        // get physical values of the forward trace
        Array<OneD, Array<OneD, NekDouble> > Fwd(nvariables);
        for (i = 0; i < nvariables; ++i)
        {
            Fwd[i] = Array<OneD, NekDouble>(nTracePts);
            m_fields[i]->ExtractTracePhys(physarray[i], Fwd[i]);
        }
        
        // Adjust the physical values of the trace to take 
        // user defined boundaries into account
        int e, id1, id2, npts;
        
        for (e = 0; e < m_fields[0]->GetBndCondExpansions()[bcRegion]
                 ->GetExpSize(); ++e)
        {
            npts = m_fields[0]->GetBndCondExpansions()[bcRegion]->
                GetExp(e)->GetTotPoints();
            id1  = m_fields[0]->GetBndCondExpansions()[bcRegion]->
                GetPhys_Offset(e);
            id2  = m_fields[0]->GetTrace()->GetPhys_Offset(
                        m_fields[0]->GetTraceMap()->
                                    GetBndCondCoeffsToGlobalCoeffsMap(cnt+e));
            
            // For 2D/3D, define: v* = v - 2(v.n)n
            Array<OneD, NekDouble> tmp(npts, 0.0);

            // Calculate (v.n)
            for (i = 0; i < m_spacedim; ++i)
            {
                Vmath::Vvtvp(npts,
                             &Fwd[1+i][id2], 1,
                             &m_traceNormals[i][id2], 1,
                             &tmp[0], 1,
                             &tmp[0], 1);    
            }

            // Calculate 2.0(v.n)
            Vmath::Smul(npts, -2.0, &tmp[0], 1, &tmp[0], 1);
            
            // Calculate v* = v - 2.0(v.n)n
            for (i = 0; i < m_spacedim; ++i)
            {
                Vmath::Vvtvp(npts,
                             &tmp[0], 1,
                             &m_traceNormals[i][id2], 1,
                             &Fwd[1+i][id2], 1,
                             &Fwd[1+i][id2], 1);
            }
            
            // copy boundary adjusted values into the boundary expansion
            for (i = 0; i < nvariables; ++i)
            {
                Vmath::Vcopy(npts, &Fwd[i][id2], 1,
                             &(m_fields[i]->GetBndCondExpansions()[bcRegion]->
                             UpdatePhys())[id1], 1);
            }
        }
    }
    

  void LinearSWE::WallBoundary2D(int bcRegion, int cnt, Array<OneD, Array<OneD, NekDouble> > &physarray)
  { 

    int i;
    int nTraceNumPoints = GetTraceTotPoints();
    int nvariables      = physarray.num_elements();
    
    // get physical values of the forward trace
    Array<OneD, Array<OneD, NekDouble> > Fwd(nvariables);
    for (i = 0; i < nvariables; ++i)
      {
	Fwd[i] = Array<OneD, NekDouble>(nTraceNumPoints);
	m_fields[i]->ExtractTracePhys(physarray[i],Fwd[i]);
      }
    
    // Adjust the physical values of the trace to take 
    // user defined boundaries into account
    int e, id1, id2, npts;
    
    for(e = 0; e < m_fields[0]->GetBndCondExpansions()[bcRegion]->GetExpSize(); ++e)
      {
	npts = m_fields[0]->GetBndCondExpansions()[bcRegion]->GetExp(e)->GetNumPoints(0);
	id1  = m_fields[0]->GetBndCondExpansions()[bcRegion]->GetPhys_Offset(e) ;
	id2  = m_fields[0]->GetTrace()->GetPhys_Offset(m_fields[0]->GetTraceMap()->GetBndCondCoeffsToGlobalCoeffsMap(cnt+e));
	
	switch(m_expdim)
	  {
	  case 1:
	    {
	      // negate the forward flux
	      Vmath::Neg(npts,&Fwd[1][id2],1);	
	    }
	    break;
	  case 2:
	    {
	      Array<OneD, NekDouble> tmp_n(npts);
	      Array<OneD, NekDouble> tmp_t(npts);
	      
	      Vmath::Vmul(npts,&Fwd[1][id2],1,&m_traceNormals[0][id2],1,&tmp_n[0],1);
	      Vmath::Vvtvp(npts,&Fwd[2][id2],1,&m_traceNormals[1][id2],1,&tmp_n[0],1,&tmp_n[0],1);
	      
	      Vmath::Vmul(npts,&Fwd[1][id2],1,&m_traceNormals[1][id2],1,&tmp_t[0],1);
	      Vmath::Vvtvm(npts,&Fwd[2][id2],1,&m_traceNormals[0][id2],1,&tmp_t[0],1,&tmp_t[0],1);
	      
	      // negate the normal flux
	      Vmath::Neg(npts,tmp_n,1);		      
	      
	      // rotate back to Cartesian
	      Vmath::Vmul(npts,&tmp_t[0],1,&m_traceNormals[1][id2],1,&Fwd[1][id2],1);
	      Vmath::Vvtvm(npts,&tmp_n[0],1,&m_traceNormals[0][id2],1,&Fwd[1][id2],1,&Fwd[1][id2],1);
	      
	      Vmath::Vmul(npts,&tmp_t[0],1,&m_traceNormals[0][id2],1,&Fwd[2][id2],1);
	      Vmath::Vvtvp(npts,&tmp_n[0],1,&m_traceNormals[1][id2],1,&Fwd[2][id2],1,&Fwd[2][id2],1);
	    }
	    break;
	  case 3:
	    ASSERTL0(false,"3D not implemented for Shallow Water Equations");
	    break;
	  default:
	    ASSERTL0(false,"Illegal expansion dimension");
	  }

	

	// copy boundary adjusted values into the boundary expansion
	for (i = 0; i < nvariables; ++i)
	  {
	    Vmath::Vcopy(npts,&Fwd[i][id2], 1,&(m_fields[i]->GetBndCondExpansions()[bcRegion]->UpdatePhys())[id1],1);
	  }
      }
  }
  
    
  // Physfield in primitive Form
 void LinearSWE::GetFluxVector(
     const Array<OneD, const Array<OneD, NekDouble> > &physfield, 
           Array<OneD, Array<OneD, Array<OneD, NekDouble> > > &flux)
  {
    int i, j;
    int nq = m_fields[0]->GetTotPoints();
    
    NekDouble g = m_g;
   
    // Flux vector for the mass equation
    for (i = 0; i < m_spacedim; ++i)
      {
       	Vmath::Vmul(nq, m_depth, 1, physfield[i+1], 1, flux[0][i], 1);
      }
    
    // Put (g eta) in tmp
     Array<OneD, NekDouble> tmp(nq);
     Vmath::Smul(nq, g, physfield[0], 1, tmp, 1);
     
     // Flux vector for the momentum equations
     for (i = 0; i < m_spacedim; ++i)
       {
	 for (j = 0; j < m_spacedim; ++j)
	   {
	     // must zero fluxes as not initialised to zero in AdvectionWeakDG ... 
	     Vmath::Zero(nq, flux[i+1][j], 1);
	   }
            
	 // Add (g eta) to appropriate field
	 Vmath::Vadd(nq, flux[i+1][i], 1, tmp, 1, flux[i+1][i], 1);
       }

  }
  
  void LinearSWE::ConservativeToPrimitive(const Array<OneD, const Array<OneD, NekDouble> >&physin,
					      Array<OneD,       Array<OneD, NekDouble> >&physout)
  {
    int nq = GetTotPoints();
      
    if(physin.get() == physout.get())
      {
	// copy indata and work with tmp array
	Array<OneD, Array<OneD, NekDouble> >tmp(3);
	for (int i = 0; i < 3; ++i)
	  {
	    // deep copy
	    tmp[i] = Array<OneD, NekDouble>(nq);
	    Vmath::Vcopy(nq,physin[i],1,tmp[i],1);
	  }
	
	// \eta = h - d
	Vmath::Vsub(nq,tmp[0],1,m_depth,1,physout[0],1);
	
	// u = hu/h
	Vmath::Vdiv(nq,tmp[1],1,tmp[0],1,physout[1],1);
	
	// v = hv/ v
	Vmath::Vdiv(nq,tmp[2],1,tmp[0],1,physout[2],1);
      }
    else
      {
	// \eta = h - d
	Vmath::Vsub(nq,physin[0],1,m_depth,1,physout[0],1);
	
	// u = hu/h
	Vmath::Vdiv(nq,physin[1],1,physin[0],1,physout[1],1);
	
	// v = hv/ v
	Vmath::Vdiv(nq,physin[2],1,physin[0],1,physout[2],1);
      }
  }


   void LinearSWE::v_ConservativeToPrimitive( )
  {
    int nq = GetTotPoints();
    
    // u = hu/h
    Vmath::Vdiv(nq,m_fields[1]->GetPhys(),1,m_fields[0]->GetPhys(),1,m_fields[1]->UpdatePhys(),1);
	
    // v = hv/ v
    Vmath::Vdiv(nq,m_fields[2]->GetPhys(),1,m_fields[0]->GetPhys(),1,m_fields[2]->UpdatePhys(),1);

    // \eta = h - d
    Vmath::Vsub(nq,m_fields[0]->GetPhys(),1,m_depth,1,m_fields[0]->UpdatePhys(),1);
  }

  void LinearSWE::PrimitiveToConservative(const Array<OneD, const Array<OneD, NekDouble> >&physin,
					     Array<OneD,       Array<OneD, NekDouble> >&physout)
  {
    
    int nq = GetTotPoints();
    
    if(physin.get() == physout.get())
      {
	// copy indata and work with tmp array
	Array<OneD, Array<OneD, NekDouble> >tmp(3);
	for (int i = 0; i < 3; ++i)
	  {
	    // deep copy
	    tmp[i] = Array<OneD, NekDouble>(nq);
	    Vmath::Vcopy(nq,physin[i],1,tmp[i],1);
	  }
	
	// h = \eta + d
	Vmath::Vadd(nq,tmp[0],1,m_depth,1,physout[0],1);
	
	// hu = h * u
	Vmath::Vmul(nq,physout[0],1,tmp[1],1,physout[1],1);
	
	// hv = h * v
	Vmath::Vmul(nq,physout[0],1,tmp[2],1,physout[2],1);
      
      }
    else
      {
	// h = \eta + d
	Vmath::Vadd(nq,physin[0],1,m_depth,1,physout[0],1);
	
	// hu = h * u
	Vmath::Vmul(nq,physout[0],1,physin[1],1,physout[1],1);
	
	// hv = h * v
	Vmath::Vmul(nq,physout[0],1,physin[2],1,physout[2],1);
	
      }
     
  }

  void LinearSWE::v_PrimitiveToConservative( )
  {
    int nq = GetTotPoints();
    
    // h = \eta + d
    Vmath::Vadd(nq,m_fields[0]->GetPhys(),1,m_depth,1,m_fields[0]->UpdatePhys(),1);
    
    // hu = h * u
    Vmath::Vmul(nq,m_fields[0]->GetPhys(),1,m_fields[1]->GetPhys(),1,m_fields[1]->UpdatePhys(),1);
    
    // hv = h * v
    Vmath::Vmul(nq,m_fields[0]->GetPhys(),1,m_fields[2]->GetPhys(),1,m_fields[2]->UpdatePhys(),1);
  }


 /**
     * @brief Compute the velocity field \f$ \mathbf{v} \f$ given the momentum
     * \f$ h\mathbf{v} \f$.
     * 
     * @param physfield  Velocity field.
     * @param velocity   Velocity field.
     */
    void LinearSWE::GetVelocityVector(
        const Array<OneD, Array<OneD, NekDouble> > &physfield,
              Array<OneD, Array<OneD, NekDouble> > &velocity)
    {
        const int npts = physfield[0].num_elements();
        
        for (int i = 0; i < m_spacedim; ++i)
        {
            Vmath::Vcopy(npts, physfield[1+i], 1, velocity[i], 1);
        }
    }


    void LinearSWE::v_GenerateSummary(SolverUtils::SummaryList& s)
    {
        ShallowWaterSystem::v_GenerateSummary(s);
	if (m_session->DefinesSolverInfo("UpwindType"))
	  {
	    std::string UpwindType;
	    UpwindType = m_session->GetSolverInfo("UpwindType");
	    if (UpwindType == "LinearAverage")
	      {
	        SolverUtils::AddSummaryItem(s, "Riemann Solver", "Linear Average");
	      }
	    if (UpwindType == "LinearHLL")
	      {
	        SolverUtils::AddSummaryItem(s, "Riemann Solver", "Linear HLL");
	      }
	  }
	SolverUtils::AddSummaryItem(s, "Variables", "eta  should be in field[0]");
	SolverUtils::AddSummaryItem(s, "",          "u    should be in field[1]");
	SolverUtils::AddSummaryItem(s, "",          "v    should be in field[2]");
    }

} //end of namespace

