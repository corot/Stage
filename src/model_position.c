///////////////////////////////////////////////////////////////////////////
//
// File: model_laser.c
// Author: Richard Vaughan
// Date: 10 June 2004
//
// CVS info:
//  $Source: /home/tcollett/stagecvs/playerstage-cvs/code/stage/src/model_position.c,v $
//  $Author: rtv $
//  $Revision: 1.4 $
//
///////////////////////////////////////////////////////////////////////////

#include <sys/time.h>
#include <math.h>

//#define DEBUG

#include "model.h"
#include "gui.h"
extern rtk_fig_t* fig_debug;

  
void position_init( model_t* mod )
{
  PRINT_DEBUG( "position startup" );
  
  // sensible position defaults
  // TODO
  
  // init the command structure
  stg_position_cmd_t cmd;
  memset( &cmd, 0, sizeof(cmd));
  cmd.mode = STG_POSITION_CONTROL_VELOCITY; // vel control mode
  cmd.x = cmd.y = cmd.a = 0; // no speeds to start
  
  model_set_command( mod, &cmd, sizeof(cmd));
  
  // set up a position-specific config structure
  stg_position_cfg_t cfg;
  memset(&cfg,0,sizeof(cfg));
  cfg.steer_mode =  STG_POSITION_STEER_DIFFERENTIAL;
  
  model_set_config( mod, &cfg, sizeof(cfg) );

  stg_position_data_t data;
  memset( &data, 0, sizeof(data) );

  model_set_data( mod, &data, sizeof(data) );
}

int position_update( model_t* mod )
{   
  PRINT_DEBUG1( "[%lu] position update", mod->world->sim_time );

  assert( mod->cfg ); // position_init should have filled these
  assert( mod->cmd );
  
  // set the model's velocity here according to the position command
  
  stg_position_cmd_t* cmd = (stg_position_cmd_t*)mod->cmd;
  stg_position_cfg_t* cfg = (stg_position_cfg_t*)mod->cfg;
  
  stg_velocity_t vel;
  memset( &vel, 0, sizeof(vel) );
  

  switch( cmd->mode )
    {
    case STG_POSITION_CONTROL_VELOCITY :
      {
	puts( "velocity control mode" );

	switch( cfg->steer_mode )
	  {
	  case STG_POSITION_STEER_DIFFERENTIAL:
	    // differential-steering model, like a Pioneer
	    vel.x = (cmd->x * cos(mod->pose.a) - cmd->y * sin(mod->pose.a));
	    vel.y = (cmd->x * sin(mod->pose.a) + cmd->y * cos(mod->pose.a));
	    vel.a = cmd->a;
	    break;

	  case STG_POSITION_STEER_INDEPENDENT:
	    // direct steering model, like an omnidirectional robot
	    vel.x = cmd->x;
	    vel.y = cmd->y;
	    vel.a = cmd->a;
	    break;

	  default:
	    PRINT_ERR1( "unknown steering mode %d", cfg->steer_mode );
	  }
      } break;
      
    case STG_POSITION_CONTROL_POSITION:
      {
	puts( "position control mode" );

	double x_error = cmd->x - mod->odom.x;
	double y_error = cmd->y - mod->odom.y;
	double a_error = NORMALIZE( cmd->a - mod->odom.a );
	
	printf( "errors: %.2f %.2f %.2f\n", x_error, y_error, a_error );

	// speed limits for controllers
	// TODO - have these configurable
	double max_speed_x = 0.5;
	double max_speed_y = 0.5;
	double max_speed_a = 2.0;	      

	switch( cfg->steer_mode )
	  {
	  case STG_POSITION_STEER_INDEPENDENT:
	    {
	      // this is easy - we just reduce the errors in each axis
	      // independently with a proportional controller, speed
	      // limited
	      vel.x = MIN( x_error, max_speed_x );
	      vel.y = MIN( y_error, max_speed_y );
	      vel.a = MIN( a_error, max_speed_a );
	    }
	    break;

	  case STG_POSITION_STEER_DIFFERENTIAL:
	    {
	      // axes can not be controlled independently. We have to
	      // turn towards the desired x,y position, drive there,
	      // then turn to face the desired angle.  this is a
	      // simple controller that works ok. Could easily be
	      // improved if anyone needs it better. Who really does
	      // position control anyhoo?

	      // start out with no velocity
	      stg_velocity_t calc;
	      memset( &calc, 0, sizeof(calc));
	      
	      double close_enough = 0.02; // fudge factor
	      
	      // if we're at the right spot
	      if( fabs(x_error) < close_enough && fabs(y_error) < close_enough )
		{
		  puts( "TURNING ON THE SPOT" );
		  // turn on the spot to minimize the error
		  calc.a = MIN( a_error, max_speed_a );
		  calc.a = MAX( a_error, -max_speed_a );
		}
	      else
		{
		  puts( "TURNING TO FACE THE GOAL POINT" );
		  // turn to face the goal point
		  double goal_angle = atan2( y_error, x_error );
		  double goal_distance = hypot( y_error, x_error );

		  a_error = NORMALIZE( goal_angle - mod->odom.a );
		  calc.a = MIN( a_error, max_speed_a );
		  calc.a = MAX( a_error, -max_speed_a );

		  printf( "steer errors: %.2f %.2f \n", a_error, goal_distance );
 
		  // if we're pointing about the right direction, move
		  // forward
		  if( fabs(a_error) < M_PI/16 )
		    {
		      puts( "DRIVING TOWARDS THE GOAL" );
		      calc.x = MIN( goal_distance, max_speed_x );
		    }
		}

	      // now set the underlying velocities using the normal
	      // diff-steer model
	      vel.x = (calc.x * cos(mod->pose.a) - calc.y * sin(mod->pose.a));
	      vel.y = (calc.x * sin(mod->pose.a) + calc.y * cos(mod->pose.a));
	      vel.a = calc.a;
	    }
	    break;
	    	    
	  default:
	    PRINT_ERR1( "unknown steering mode %d", cfg->steer_mode );
	  }
      }
      break;

    default:
      PRINT_ERR1( "unhandled position command mode %d", cmd->mode );
    }

  model_set_velocity( mod, &vel );
  
  
  double interval = (double)mod->world->sim_interval / 1000.0;
  
  // set the data here
  stg_position_data_t data;
  memcpy( &data.pose, &mod->odom, sizeof(stg_pose_t));  
  memcpy( &data.velocity, &mod->velocity, sizeof(stg_velocity_t));
  
  data.stall = mod->stall;

  // publish the data
  model_set_data( mod, &data, sizeof(data));
  
  return 0; //ok
}

int position_startup( model_t* mod )
{
  PRINT_DEBUG( "position startup" );
  return 0; // ok
}

int position_shutdown( model_t* mod )
{
  PRINT_DEBUG( "position shutdown" );
  
  // re-initialize the device - safety feature!
  position_init(mod);

  return 0; // ok
}


int position_handle_message(model_t* mod, int fd, stg_msg_t* msg)
{
  PRINT_WARN( "POSITION RECEIVED MESSAGE!" );
  
  return 0; // message handled ok
}


int register_position( lib_entry_t* lib )
{ 
  assert(lib);
  
  lib[STG_MODEL_POSITION].init = position_init;
  lib[STG_MODEL_POSITION].update = position_update;
  lib[STG_MODEL_POSITION].shutdown = position_shutdown;
  lib[STG_MODEL_POSITION].startup = position_startup;
  lib[STG_MODEL_POSITION].handle_message = position_handle_message;

  return 0; //ok
} 
