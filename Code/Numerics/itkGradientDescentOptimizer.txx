/*=========================================================================

  Program:   Insight Segmentation & Registration Toolkit
  Module:    itkGradientDescentOptimizer.txx
  Language:  C++
  Date:      $Date$
  Version:   $Revision$


  Copyright (c) 2000 National Library of Medicine
  All rights reserved.

  See COPYRIGHT.txt for copyright details.

=========================================================================*/
#ifndef _itkGradientDescentOptimizer_txx
#define _itkGradientDescentOptimizer_txx

#include "itkGradientDescentOptimizer.h"

namespace itk
{

/**
 * Constructor
 */
template <class TCostFunction>
GradientDescentOptimizer<TCostFunction>
::GradientDescentOptimizer()
{
}



/**
 * Start the optimization
 */
template <class TCostFunction>
void
GradientDescentOptimizer<TCostFunction>
::StartOptimization( void )
{
  m_CurrentStepLength         = m_MaximumStepLength;
  m_CurrentNumberIterations   = 0;
  ResumeOptimization();
}





/**
 * Resume the optimization
 */
template <class TCostFunction>
void
GradientDescentOptimizer<TCostFunction>
::ResumeOptimization( void )
{
  
  m_Stop = false;

  while( !m_Stop ) 
  {
    m_Value = m_CostFunction->GetValue( GetCurrentPosition() );

    if( m_Stop )
    {
      break;
    }

    m_PreviousGradient = m_Gradient;
  
    m_Gradient = m_CostFunction->GetDerivative( GetCurrentPosition() );

    if( m_Stop )
    {
      break;
    }

    AdvanceOneStep();

    m_CurrentNumberIterations++;

    if( m_CurrentNumberIterations == m_MaximumNumberOfIterations )
    {
       m_StopCondition = MaximumNumberOfIterations;
       StopOptimization();
       break;
    }
    
  }
    

}





/**
 * Stop optimization
 */
template <class TCostFunction>
void
GradientDescentOptimizer<TCostFunction>
::StopOptimization( void )
{
  m_Stop = true;
}


/**
 * Advance one Step following the gradient direction
 */
template <class TCostFunction>
void
GradientDescentOptimizer<TCostFunction>
::AdvanceOneStep( void )
{ 

  double magnitudeSquare = 0;
  for(unsigned int i=0; i<SpaceDimension; i++)
  {
    const double weighted = m_Gradient[i] * m_StepSize[i];
    magnitudeSquare += weighted * weighted;
  }
    
  const double gradientMagnitude = sqrt( magnitudeSquare );

  if( gradientMagnitude < m_GradientMagnitudeTolerance ) 
  {
    m_StopCondition = GradientMagnitudeTolerance;
    StopOptimization();
    return;
  }
    
  double scalarProduct = 0;

  for(unsigned int i=0; i<SpaceDimension; i++)
  {
    const double weight1 = m_Gradient[i]         * m_StepSize[i]; 
    const double weight2 = m_PreviousGradient[i] * m_StepSize[i]; 
    scalarProduct += weight1 * weight2;
  }
   
  // If there is a direction change 
  if( scalarProduct < 0 ) 
  {
    m_CurrentStepLength /= 2.0;
  }

  if( m_CurrentStepLength < m_MinimumStepLength )
  {
    m_StopCondition = StepTooSmall;
    StopOptimization();
    return;
  }

  double direction = 1.0;
  if( this->m_Maximize ) 
  {
    direction = 1.0;
  }
  else 
  {
    direction = -1.0;
  }

  ParametersType newPosition;
  const ParametersType & currentPosition = GetCurrentPosition();
  const double factor = 
    (direction * m_CurrentStepLength / gradientMagnitude);

  for(unsigned int i=0; i<SpaceDimension; i++)
  {
    newPosition[i] = currentPosition[i] + m_Gradient[i] * factor;
  }

  SetCurrentPosition( newPosition );

}



} // end namespace itk

#endif
