/*=========================================================================

  Program:   Insight Segmentation & Registration Toolkit
  Module:    itkFEMSolver.cxx
  Language:  C++
  Date:      $Date$
  Version:   $Revision$

  Copyright (c) 2002 Insight Consortium. All rights reserved.
  See ITKCopyright.txt or http://www.itk.org/HTML/Copyright.htm for details.

     This software is distributed WITHOUT ANY WARRANTY; without even 
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR 
     PURPOSE.  See the above copyright notices for more information.

=========================================================================*/

// disable debug warnings in MS compiler
#ifdef _MSC_VER
#pragma warning(disable: 4786)
#endif

#include "itkFEMSolver.h"

#include "itkFEMLoadNode.h"
#include "itkFEMLoadElementBase.h"
#include "itkFEMElementNewBase.h"
#include "itkFEMLoadBC.h"
#include "itkFEMLoadBCMFC.h"

#include <algorithm>

namespace itk {
namespace fem {




/**
 * Default constructor for Solver class
 */
Solver::Solver() : NGFN(0), NMFC(0)
{
  this->SetLinearSystemWrapper(&m_lsVNL);
}




void Solver::Clear( void )
{
  this->el.clear();
  this->node.clear();
  this->mat.clear();
  this->load.clear();

  this->NGFN=0;
  this->NMFC=0;
  this->SetLinearSystemWrapper(&m_lsVNL);
}




/*
 * Change the LinearSystemWrapper object used to solve
 * system of equations.
 */
void Solver::SetLinearSystemWrapper(LinearSystemWrapper::Pointer ls)
{ 
  m_ls=ls; // update the pointer to LinearSystemWrapper object

  this->InitializeLinearSystemWrapper();
}




void Solver::InitializeLinearSystemWrapper(void)
{ 
  // set the maximum number of matrices and vectors that
  // we will need to store inside.
  m_ls->SetNumberOfMatrices(1);
  m_ls->SetNumberOfVectors(2);
  m_ls->SetNumberOfSolutions(1);
}




/**
 * Reads the whole system (nodes, materials and elements) from input stream
 */
void Solver::Read(std::istream& f) {

  // clear all arrays
  el.clear();
  node.clear();
  mat.clear();
  load.clear();

  // Initialize the pointers to arrays in ReadInfoType object to the
  // arrays in solver object.
  ReadInfoType info(&this->node,&this->el,&this->mat);

  FEMLightObject::Pointer o=0;
  /* then we start reading objects from stream */
  do
  {
    o=FEMLightObject::CreateFromStream(f,&info);
    /*
     * If CreateFromStream returned 0, we're ok. That was the signal
     * for the end of stream. Just continue reading... and consequently
     * exit the do loop.
     */
    if (!o) { continue; }

    /*
     * Find out what kind of object did we read from stream
     * and store it in the appropriate array
     */
    if ( Node::Pointer o1=dynamic_cast<Node*>(&*o) )
    {
      node.push_back(FEMP<Node>(o1));
      continue;
    }
    if ( Material::Pointer o1=dynamic_cast<Material*>(&*o) )
    {
      mat.push_back(FEMP<Material>(o1));
      continue;
    }
    if ( Element::Pointer o1=dynamic_cast<Element*>(&*o) )
    {
      el.push_back(FEMP<Element>(o1));
      continue;
    }
    if ( Load::Pointer o1=dynamic_cast<Load*>(&*o) )
    {
      load.push_back(FEMP<Load>(o1));
      continue;
    }

    /*
     * If we got here, something strange was in the file...
     */

    // first we delete the allocated object
    #ifndef FEM_USE_SMART_POINTERS
    delete o;
    #endif
    o=0;

    // then we throw an exception
    throw FEMExceptionIO(__FILE__,__LINE__,"Solver::Read()","Error reading FEM problem stream!");

  } while ( o );

}




/**
 * Writes everything (nodes, materials and elements) to output stream
 */
void Solver::Write( std::ostream& f ) {

  for(NodeArray::iterator i=node.begin(); i!=node.end(); i++) {
    (*i)->Write(f);
  }
  f<<"\n<END>  % End of nodes\n\n";
  
  for(MaterialArray::iterator i=mat.begin(); i!=mat.end(); i++) {
    (*i)->Write(f);
  }
  f<<"\n<END>  % End of materials\n\n";

  for(ElementArray::iterator i=el.begin(); i!=el.end(); i++) {
    (*i)->Write(f);
  }
  f<<"\n<END>  % End of elements\n\n";

  for(LoadArray::iterator i=load.begin(); i!=load.end(); i++) {
    (*i)->Write(f);
  }
  f<<"\n<END>  % End of loads\n\n";


}




/**
 * Assign a global freedom number to each DOF in a system.
 */
void Solver::GenerateGFN() {

  // Clear the list of elements in nodes
  // FIXME: should be removed once Mesh is there
  for(NodeArray::iterator n=node.begin(); n!=node.end(); n++)
  {
   (*n)->m_elements.clear();
  }

  // first we have to clear the global freedom numbers (GFN) in all DOF
  for(ElementArray::iterator e=el.begin(); e!=el.end(); e++) // step over all elements
  {
    // Clear DOF IDs in an element
    (*e)->ClearDegreesOfFreedom();

    // Add the elemens in the nodes list of elements
    // FIXME: should be removed once Mesh is there
    unsigned int Npts=(*e)->GetNumberOfPoints();
    for(unsigned int pt=0; pt<Npts; pt++)
    {
      (*e)->GetPoint(pt)->m_elements.insert(*e);
    }
  }


  /*
   * Assign new ID to every DOF in a system
   */

  // Start numbering DOFs from 0
  NGFN=0;

  Element::NodeDefinitionType ndef,nndef;

  // Step over all elements
  for(ElementArray::iterator e=el.begin(); e!=el.end(); e++)
  {

    // Handle DOFs in new elements
    // FIXME:
    if(ElementNew::Pointer elem=dynamic_cast<ElementNew*>(&**e))
    {
      for(unsigned int n=0; n<elem->GetNumberOfNodes(); n++)
      {
        for(unsigned int dof=0; dof<elem->GetNumberOfDegreesOfFreedomPerNode(); dof++)
        {
          if( elem->GetNode(n)->GetDegreeOfFreedom(dof)==ElementNew::InvalidDegreeOfFreedomID )
          {
            elem->GetNode(n)->SetDegreeOfFreedom(dof,NGFN);
            NGFN++;
          }
        }
      }
      continue;
    }

    // FIXME: Write a code that checks if two elements are compatible, when they share a node.

    // Define some frequently used constants
    const unsigned int Nnodes=(*e)->GetNumberOfNodes();
    const unsigned int Npoints=(*e)->GetNumberOfPoints();
    const unsigned int NDOFsperNode=(*e)->GetNumberOfDegreesOfFreedomPerNode();

    // We step over all nodes in current element
    // and try to find a matching node in neighboring element
    for( unsigned int n=0; n<Nnodes; n++ )
    {
      // Get the definition of a current node
      (*e)->GetNodeDefinition(n,ndef);
      std::sort(ndef.begin(),ndef.end());

      // Flag to exit subsequent for loops before they finish
      bool not_done=true;

      // Try to find the matching node definition in neighborhood elements

      // Step over all neighboring elements
      for( unsigned int pt=0; pt<Npoints && not_done; pt++ )
      {
        Element::PointIDType p=(*e)->GetPoint(pt);

        Node::SetOfElements::const_iterator el_it_end=p->m_elements.end();
        for( Node::SetOfElements::const_iterator el_it=p->m_elements.begin();
             el_it!=el_it_end && not_done;
             el_it++ )
        {
          // Skip current element
          if((*el_it)==(&**e)) continue;

          // Step over all nodes in this neigboring element
          for( int nn=(*el_it)->GetNumberOfNodes()-1; nn>=0 && not_done; nn-- )
          {
            // Get the definition of a node
            (*el_it)->GetNodeDefinition(nn,nndef);
            std::sort(nndef.begin(),nndef.end());

            if(ndef==nndef)
            {
              // We found a node that is shared between elements.
              // Copy the DOFs from the neighboring element's node
              // since they have to be the same.
              //
              // Note that neighboring node may contain more or less DOFs.
              // If it has more, we simply ignore the rest, if it has less,
              // we'll get invalid DOF id from GetDegreeOfFreedomAtNode function.

              // If all DOF IDs are set from the neighboring elements,
              // we can terminate the loop over all nodes in
              // neighboring elements.
              not_done=false;

              for(int d=NDOFsperNode-1; d>=0; d--)
              {
                // Get the DOF from the node at neighboring element
                Element::DegreeOfFreedomIDType global_dof = (*el_it)->GetDegreeOfFreedomAtNode(nn,d);

                // Set the corresponding DOF in current element only if
                // we find a valid DOF id in the neighboring element
                if( global_dof!=Element::InvalidDegreeOfFreedomID )
                {
                  // Error checking
                  if( (*e)->GetDegreeOfFreedomAtNode(n,d)!=Element::InvalidDegreeOfFreedomID && 
                      (*e)->GetDegreeOfFreedomAtNode(n,d)!=global_dof)
                  {
                    // Something got screwed.
                    // FIXME: Write a better error handler or remove it completely,
                    //        since this should never happen.
                    throw FEMException(__FILE__, __LINE__, "FEM error");
                  }

                  (*e)->SetDegreeOfFreedomAtNode(n,d,global_dof);

                }
                else
                {
                  // Whenever we find an invalid DOF ID, we are not done yet.
                  not_done=true;
                }

              } // end for d

            } // end if ndef==nndef

          } // end for nn
  
        } // end for el_it

      } // end for pt


      // Now all DOFs in current element for node n are matched with those
      // in the neghboring elements. However, if none of the neighboring
      // objects defines these DOFs, we need to assign new DOF IDs here.
      for(unsigned int d=0; d<NDOFsperNode; d++) // step over all DOFs at node n
      {
        if( (*e)->GetDegreeOfFreedomAtNode(n,d)==Element::InvalidDegreeOfFreedomID )
        {
          // Found a undefined DOF. We need obtain a unique id,
          // which we set with the SetDegreeOfFreedom function.
          (*e)->SetDegreeOfFreedomAtNode(n,d,NGFN);
          NGFN++;
        }

      } // end for d

    } // end for n



  } // end for e

//  NGFN=Element::GetGlobalDOFCounter()+1;
  if (NGFN>0) return;  // if we got 0 DOF, somebody forgot to define the system...

}




/**
 * Assemble the master stiffness matrix (also apply the MFCs to K)
 */  
void Solver::AssembleK()
{

  // if no DOFs exist in a system, we have nothing to do
  if (NGFN<=0) return;

  NMFC=0;  // reset number of MFC in a system

  /*
   * Before we can start the assembly procedure, we need to know,
   * how many boundary conditions if form of MFCs are there in a system.
   */

  // search for MFC's in Loads array, because they affect the master stiffness matrix
  for(LoadArray::iterator l=load.begin(); l!=load.end(); l++)
  {
    if ( LoadBCMFC::Pointer l1=dynamic_cast<LoadBCMFC*>( &(*(*l))) ) {
      // store the index of an LoadBCMFC object for later
      l1->Index=NMFC;
      // increase the number of MFC
      NMFC++;
    }
  }

  /*
   * Now we can assemble the master stiffness matrix from
   * element stiffness matrices.
   *
   * Since we're using the Lagrange multiplier method to apply the MFC,
   * each constraint adds a new global DOF.
   */
  this->InitializeMatrixForAssembly(NGFN+NMFC);

  /*
   * Step over all elements
   */
  for(ElementArray::iterator e=el.begin(); e!=el.end(); e++)
  {
    // Call the function that actually moves the element matrix
    // to the master matrix.
    this->AssembleElementMatrix(*e);
  }

  this->FinalizeMatrixAfterAssembly();

}




void Solver::InitializeMatrixForAssembly(unsigned int N)
{
  // We use LinearSystemWrapper object, to store the K matrix.
  this->m_ls->SetSystemOrder(N);
  this->m_ls->InitializeMatrix();
}




void Solver::AssembleElementMatrix(Element::Pointer e)
{
  // Copy the element stiffness matrix for faster access.
  Element::MatrixType Ke=e->Ke();

  // ... same for number of DOF
  int Ne=e->GetNumberOfDegreesOfFreedom();

  // step over all rows in element matrix
  for(int j=0; j<Ne; j++)
  {
    // step over all columns in element matrix
    for(int k=0; k<Ne; k++) 
    {
      // error checking. all GFN should be =>0 and <NGFN
      if ( e->GetDegreeOfFreedom(j) >= NGFN ||
           e->GetDegreeOfFreedom(k) >= NGFN  )
      {
        throw FEMExceptionSolution(__FILE__,__LINE__,"Solver::AssembleElementMatrix()","Illegal GFN!");
      }

      /*
       * Here we finaly update the corresponding element
       * in the master stiffness matrix. We first check if 
       * element in Ke is zero, to prevent zeros from being 
       * allocated in sparse matrix.
       */
      if ( Ke[j][k]!=Float(0.0) )
      {
        this->m_ls->AddMatrixValue( e->GetDegreeOfFreedom(j), e->GetDegreeOfFreedom(k), Ke[j][k] );
      }

    }

  }

}




/**
 * Assemble the master force vector
 */
void Solver::AssembleF(int dim) {

  // Type that stores IDs of fixed DOF together with the values to
  // which they were fixed.
  typedef std::map<Element::DegreeOfFreedomIDType,Float> BCTermType;
  BCTermType bcterm;

  /* if no DOFs exist in a system, we have nothing to do */
  if (NGFN<=0) return;
  
  /* Initialize the master force vector */
  m_ls->InitializeVector();

  /*
   * Convert the external loads to the nodal loads and
   * add them to the master force vector F.
   */
  for(LoadArray::iterator l=load.begin(); l!=load.end(); l++) {

    /*
     * Store a temporary pointer to load object for later,
     * so that we don't have to access it via the iterator
     */
    Load::Pointer l0=*l;

    /*
     * Pass the vector to the solution to the Load object.
     */
    l0->SetSolution(m_ls);

    /*
     * Here we only handle Nodal loads
     */
    if ( LoadNode::Pointer l1=dynamic_cast<LoadNode*>(&*l0) ) {
      // yep, we have a nodal load

      // size of a force vector in load must match number of DOFs in node
      if ( (l1->F.size() % l1->m_element->GetNumberOfDegreesOfFreedomPerNode())!=0 )
      {
        throw FEMExceptionSolution(__FILE__,__LINE__,"Solver::AssembleF()","Illegal size of a force vector in LoadNode object!");
      }

      // we simply copy the load to the force vector
      for(unsigned int dof=0; dof < (l1->m_element->GetNumberOfDegreesOfFreedomPerNode()); dof++)
      {
        // error checking
        if ( l1->m_element->GetDegreeOfFreedomAtNode(l1->m_pt,dof) >= NGFN )
        {
          throw FEMExceptionSolution(__FILE__,__LINE__,"Solver::AssembleF()","Illegal GFN!");
        }

        /*
         * If using the extra dim parameter, we can apply the force to different isotropic dimension.
         *
         * FIXME: We assume that the implementation of force vector inside the LoadNode class is correct for given
         * number of dimensions.
         */
        m_ls->AddVectorValue(l1->m_element->GetDegreeOfFreedomAtNode(l1->m_pt,dof) , l1->F[dof+l1->m_element->GetNumberOfDegreesOfFreedomPerNode()*dim]);
      }

      // that's all there is to DOF loads, go to next load in an array
      continue;  
    }


    /*
     * Element loads...
     */
    if ( LoadElement::Pointer l1=dynamic_cast<LoadElement*>(&*l0) )
    {

      if ( !(l1->el.empty()) )
      {
        /*
         * If array of element pointers is not empty,
         * we apply the load to all elements in that array
         */
        for(LoadElement::ElementPointersVectorType::const_iterator i=l1->el.begin(); i!=l1->el.end(); i++)
        {

          const Element* el0=(*i);
          // call the Fe() function of the element that we are applying the load to.
          // we pass a pointer to the load object as a paramater.
          vnl_vector<Float> Fe = el0->Fe(Element::LoadElementPointer(l1));
          unsigned int Ne=el0->GetNumberOfDegreesOfFreedom();          // ... element's number of DOF
          for(unsigned int j=0; j<Ne; j++)    // step over all DOF
          {
            // error checking
            if ( el0->GetDegreeOfFreedom(j) >= NGFN )
            {
              throw FEMExceptionSolution(__FILE__,__LINE__,"Solver::AssembleF()","Illegal GFN!");
            }

            // update the master force vector (take care of the correct isotropic dimensions)
            m_ls->AddVectorValue(el0->GetDegreeOfFreedom(j) , Fe(j+dim*Ne));
          }
        }
      
      } else {
        
        /*
         * If the list of element pointers in load object is empty,
         * we apply the load to all elements in a system.
         */
        for(ElementArray::iterator e=el.begin(); e!=el.end(); e++) // step over all elements in a system
        {
          vnl_vector<Float> Fe=(*e)->Fe(Element::LoadElementPointer(l1));  // ... element's force vector
          unsigned int Ne=(*e)->GetNumberOfDegreesOfFreedom();          // ... element's number of DOF

          for(unsigned int j=0; j<Ne; j++)        // step over all DOF
          {
            if ( (*e)->GetDegreeOfFreedom(j) >= NGFN )
            {
              throw FEMExceptionSolution(__FILE__,__LINE__,"Solver::AssembleF()","Illegal GFN!");
            }

            // update the master force vector (take care of the correct isotropic dimensions)
            m_ls->AddVectorValue((*e)->GetDegreeOfFreedom(j) , Fe(j+dim*Ne));

          }

        }

      }

      // skip to next load in an array
      continue;
    }

    /*
     * Handle boundary conditions in form of MFC loads are handled next.
     */
    if ( LoadBCMFC::Pointer l1=dynamic_cast<LoadBCMFC*>(&*l0) )
    {
      m_ls->SetVectorValue(NGFN+l1->Index , l1->rhs[dim]);

      // skip to next load in an array
      continue;
    }

    /*
     * Handle essential boundary conditions.
     */
    if ( LoadBC::Pointer l1=dynamic_cast<LoadBC*>(&*l0) )
    {

      // Here we just store the values of fixed DOFs. We can't set it here, because
      // it may be changed by other loads that are applied later.
      bcterm[ l1->m_element->GetDegreeOfFreedom(l1->m_dof) ]=l1->m_value[dim];

       // skip to the next load in an array
      continue;
    }

    /*
     * If we got here, we were unable to handle that class of Load object.
     * We do nothing...
     */


  }  // for(LoadArray::iterator l ... )

  /*
   * Adjust the master force vector for essential boundary
   * conditions as required.
   */
  if ( m_ls->IsVectorInitialized(1) )
  {
    // Add the vector generated by ApplyBC to the solution vector
    const unsigned int totGFN=NGFN+NMFC;
    for( unsigned int i=0; i<totGFN; i++ )
    {
      m_ls->AddVectorValue(i,m_ls->GetVectorValue(i,1));
    }

  }

  // Set the fixed DOFs to proper values
  for( BCTermType::iterator q=bcterm.begin(); q!=bcterm.end(); q++)
  {
    m_ls->SetVectorValue(q->first,q->second);
  }


}



/**
 * Decompose matrix using svd, qr, whatever ... if needed
 */  
void Solver::DecomposeK()
{
}




/**
 * Solve for the displacement vector u
 */  
void Solver::Solve()
{
  // Check if master stiffness matrix and master force vector were
  // properly initialized.
  if(!m_ls->IsMatrixInitialized())
  {
    throw FEMExceptionSolution(__FILE__,__LINE__,"Solver::Solve()","Master stiffness matrix was not initialized!");
  }
  if(!m_ls->IsVectorInitialized())
  {
    throw FEMExceptionSolution(__FILE__,__LINE__,"Solver::Solve()","Master force vector was not initialized!");
  }

  // Solve the system of linear equations
  m_ls->InitializeSolution();
  m_ls->Solve();
}




/**
 * Copy solution vector u to the corresponding nodal values, which are
 * stored in node objects). This is standard post processing of the solution.
 */  
void Solver::UpdateDisplacements()
{

}




/**
 * Apply the boundary conditions to the system.
 */
void Solver::ApplyBC(int dim, unsigned int matrix)
{

  // Vector with index 1 is used to store force correctios for BCs
  m_ls->DestroyVector(1);

  /* Step over all Loads */
  for(LoadArray::iterator l=load.begin(); l!=load.end(); l++)
  {

    /*
     * Store a temporary pointer to load object for later,
     * so that we don't have to access it via the iterator
     */
    Load::Pointer l0=*l;


    /*
     * Apply boundary conditions in form of MFC loads.
     *
     * We add the multi freedom constraints contribution to the master
     * stiffness matrix using the lagrange multipliers. Basically we only
     * change the last couple of rows and columns in K.
     */
    if ( LoadBCMFC::Pointer c=dynamic_cast<LoadBCMFC*>(&*l0) )
    {
      /* step over all DOFs in MFC */
      for(LoadBCMFC::LhsType::iterator q=c->lhs.begin(); q!=c->lhs.end(); q++) {
      
        /* obtain the GFN of DOF that is in the MFC */
        Element::DegreeOfFreedomIDType gfn=q->m_element->GetDegreeOfFreedom(q->dof);

        /* error checking. all GFN should be =>0 and <NGFN */
        if ( gfn>=NGFN )
        {
          throw FEMExceptionSolution(__FILE__,__LINE__,"Solver::ApplyBC()","Illegal GFN!");
        }

        /* set the proper values in matster stiffnes matrix */
        this->m_ls->SetMatrixValue(gfn, NGFN+c->Index, q->value, matrix);
        this->m_ls->SetMatrixValue(NGFN+c->Index, gfn, q->value, matrix);  // this is a symetric matrix...

      }

      // skip to next load in an array
      continue;
    }



    /*
     * Apply essential boundary conditions
     */
    if ( LoadBC::Pointer c=dynamic_cast<LoadBC*>(&*l0) )
    {

      Element::DegreeOfFreedomIDType fdof = c->m_element->GetDegreeOfFreedom(c->m_dof);
      Float fixedvalue=c->m_value[dim];


      // Copy the corresponding row of the matrix to the vector that will
      // be later added to the master force vector.
      // NOTE: We need to copy the whole row first, and then clear it. This
      //       is much more efficient when using sparse matrix storage, than
      //       copying and clearing in one loop.

      // Get the column indices of the nonzero elements in an array.
      LinearSystemWrapper::ColumnArray cols;
      m_ls->GetColumnsOfNonZeroMatrixElementsInRow(fdof, cols, matrix);

      // Force vector needs updating only if DOF was not fixed to 0.0.
      if( fixedvalue!=0.0 )
      {
        // Initialize the master force correction vector as required
        if ( !this->m_ls->IsVectorInitialized(1) )
        {
          this->m_ls->InitializeVector(1);
        }

        // Step over each nonzero matrix element in a row
        for(LinearSystemWrapper::ColumnArray::iterator c=cols.begin(); c!=cols.end(); c++)
        {
          // Get value from the stiffness matrix
          Float d=this->m_ls->GetMatrixValue(fdof, *c, matrix);

          // Store the appropriate value in bc correction vector (-K12*u2)
          //
          // See http://titan.colorado.edu/courses.d/IFEM.d/IFEM.Ch04.d/IFEM.Ch04.pdf
          // chapter 4.1.3 (Matrix Forms of DBC Application Methods) for more info.
          this->m_ls->AddVectorValue(*c,-d*fixedvalue,1);
        }
      }


      // Clear that row and column in master matrix
      for(LinearSystemWrapper::ColumnArray::iterator c=cols.begin(); c!=cols.end(); c++)
      {
        this->m_ls->SetMatrixValue(fdof,*c, 0.0, matrix);
        this->m_ls->SetMatrixValue(*c,fdof, 0.0, matrix); // this is a symetric matrix
      }
      this->m_ls->SetMatrixValue(fdof,fdof, 1.0, matrix); // Set the diagonal element to one


      // skip to next load in an array
      continue;

    }


  } // end for LoadArray::iterator l


}




}} // end namespace itk::fem
