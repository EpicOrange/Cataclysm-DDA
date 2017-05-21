#include "game.h"
#include "map.h"
#include "player.h"
#include "vehicle.h"
#include "messages.h"
#include "sounds.h"

/*
bool game::grabbed_veh_move( const tripoint &dp )
{
    int grabbed_part = 0;
    vehicle *grabbed_vehicle = m.veh_at( u.pos() + u.grab_point, grabbed_part );
    if( nullptr == grabbed_vehicle ) {
        add_msg( m_info, _( "No vehicle at grabbed point." ) );
        u.grab_point = tripoint_zero;
        u.grab_type = OBJECT_NONE;
        return false;
    }

    const vehicle *veh_under_player = m.veh_at( u.pos() );
    if( grabbed_vehicle == veh_under_player ) {
        u.grab_point = -dp;
        return false;
    }

    tripoint dp_veh = -u.grab_point;
    tripoint prev_grab = u.grab_point;
    tripoint next_grab = u.grab_point;

    bool zigzag = false;

    if( dp == prev_grab ) {
        // We are pushing in the direction of veh
        dp_veh = dp;
    } else if( abs( dp.x + dp_veh.x ) != 2 && abs( dp.y + dp_veh.y ) != 2 ) {
        // Not actually moving the vehicle, don't do the checks
        u.grab_point = -( dp + dp_veh );
        return false;
    } else if( ( dp.x == prev_grab.x || dp.y == prev_grab.y ) &&
               next_grab.x != 0 && next_grab.y != 0 ) {
        // Zig-zag (or semi-zig-zag) pull: player is diagonal to veh
        // and moves away from it, but not directly away
        dp_veh.x = ( dp.x == -dp_veh.x ) ? 0 : dp_veh.x;
        dp_veh.y = ( dp.y == -dp_veh.y ) ? 0 : dp_veh.y;

        next_grab = -dp_veh;
        zigzag = true;
    } else {
        // We are pulling the veh
        next_grab = -dp;
    }

    // Make sure the mass and pivot point are correct
    grabbed_vehicle->invalidate_mass();

    //vehicle movement: strength check
    int mc = 0;
    int str_req = ( grabbed_vehicle->total_mass() / 25 ); //strengh reqired to move vehicle.

    //if vehicle is rollable we modify str_req based on a function of movecost per wheel.

    // Veh just too big to grab & move; 41-45 lets folks have a bit of a window
    // (Roughly 1.1K kg = danger zone; cube vans are about the max)
    if( str_req > 45 ) {
        add_msg( m_info, _( "The %s is too bulky for you to move by hand." ),
                 grabbed_vehicle->name.c_str() );
        return true; // No shoving around an RV.
    }

    const auto &wheel_indices = grabbed_vehicle->wheelcache;
    if( grabbed_vehicle->valid_wheel_config( false ) ) {
        //determine movecost for terrain touching wheels
        const tripoint vehpos = grabbed_vehicle->global_pos3();
        for( int p : wheel_indices ) {
            const tripoint wheel_pos = vehpos + grabbed_vehicle->parts[p].precalc[0];
            const int mapcost = m.move_cost( wheel_pos, grabbed_vehicle );
            mc += ( str_req / wheel_indices.size() ) * mapcost;
        }
        //set strength check threshold
        //if vehicle has many or only one wheel (shopping cart), it is as if it had four.
        if( wheel_indices.size() > 4 || wheel_indices.size() == 1 ) {
            str_req = mc / 4 + 1;
        } else {
            str_req = mc / wheel_indices.size() + 1;
        }
    } else {
        str_req++;
        //if vehicle has no wheels str_req make a noise.
        if( str_req <= u.get_str() ) {
            sounds::sound( grabbed_vehicle->global_pos3(), str_req * 2,
                           _( "a scraping noise." ) );
        }
    }

    //final strength check and outcomes
    ///\EFFECT_STR determines ability to drag vehicles
    if( str_req <= u.get_str() ) {
        //calculate exertion factor and movement penalty
        ///\EFFECT_STR increases speed of dragging vehicles
        u.moves -= 100 * str_req / std::max( 1, u.get_str() );
        int ex = dice( 1, 3 ) - 1 + str_req;
        if( ex > u.get_str() ) {
            add_msg( m_bad, _( "You strain yourself to move the %s!" ), grabbed_vehicle->name.c_str() );
            u.moves -= 200;
            u.mod_pain( 1 );
        } else if( ex == u.get_str() ) {
            u.moves -= 200;
            add_msg( _( "It takes some time to move the %s." ), grabbed_vehicle->name.c_str() );
        }
    } else {
        u.moves -= 100;
        add_msg( m_bad, _( "You lack the strength to move the %s" ), grabbed_vehicle->name.c_str() );
        return true;
    }

    std::string blocker_name = _( "errors in movement code" );
    const auto get_move_dir = [&]( const tripoint & dir, const tripoint & from ) {
        tileray mdir;

        mdir.init( dir.x, dir.y );
        grabbed_vehicle->turn( mdir.dir() - grabbed_vehicle->face.dir() );
        grabbed_vehicle->face = grabbed_vehicle->turn_dir;
        grabbed_vehicle->precalc_mounts( 1, mdir.dir(), grabbed_vehicle->pivot_point() );

        // Grabbed part has to stay at distance 1 to the player
        // and in roughly the same direction.
        const tripoint new_part_pos = grabbed_vehicle->global_pos3() +
                                      grabbed_vehicle->parts[ grabbed_part ].precalc[ 1 ];
        const tripoint expected_pos = u.pos() + dp + from;
        const tripoint actual_dir = expected_pos - new_part_pos;

        // Set player location to illegal value so it can't collide with vehicle.
        const tripoint player_prev = u.pos();
        u.setpos( tripoint_zero );
        std::vector<veh_collision> colls;
        bool failed = grabbed_vehicle->collision( colls, actual_dir, true );
        u.setpos( player_prev );
        if( !colls.empty() ) {
            blocker_name = colls.front().target_name;
        }
        return failed ? tripoint_zero : actual_dir;
    };

    // First try the move as intended
    // But if that fails and the move is a zig-zag, try to recover:
    // Try to place the vehicle in the position player just left rather than "flattening" the zig-zag
    tripoint final_dp_veh = get_move_dir( dp_veh, next_grab );
    if( final_dp_veh == tripoint_zero && zigzag ) {
        final_dp_veh = get_move_dir( -prev_grab, -dp );
        next_grab = -dp;
    }

    if( final_dp_veh == tripoint_zero ) {
        add_msg( _( "The %s collides with %s." ), grabbed_vehicle->name.c_str(), blocker_name.c_str() );
        u.grab_point = prev_grab;
        return true;
    }

    u.grab_point = next_grab;

    tripoint gp = grabbed_vehicle->global_pos3();
    grabbed_vehicle = m.displace_vehicle( gp, final_dp_veh );

    if( grabbed_vehicle == nullptr ) {
        debugmsg( "Grabbed vehicle disappeared" );
        return false;
    }

    for( int p : wheel_indices ) {
        if( one_in( 2 ) ) {
            tripoint wheel_p = grabbed_vehicle->global_part_pos3( grabbed_part );
            grabbed_vehicle->handle_trap( wheel_p, p );
        }
    }

    return false;

}

/*/

bool game::grabbed_veh_move( const tripoint &dp )
{
    int grabbed_part = 0;
    tripoint grab_position = u.pos() + u.grab_point; // relative to map
    vehicle *grabbed_vehicle = m.veh_at( grab_position, grabbed_part );
    if( nullptr == grabbed_vehicle ) {
        add_msg( m_info, _("No vehicle at grabbed point.") );
        u.grab_point = tripoint_zero;
        u.grab_type = OBJECT_NONE;
        return false;
    }

    const vehicle *veh_under_player = m.veh_at( u.pos() );
    if( grabbed_vehicle == veh_under_player ) {
        u.grab_point = -dp;
        return false;
    }

    tripoint dp_veh = -u.grab_point;
    tripoint prev_grab = u.grab_point;
    tripoint next_grab = u.grab_point;

    bool zigzag = false;

    if( dp == prev_grab ) {
        // We are pushing in the direction of veh
        dp_veh = dp;
    } else if( abs( dp.x + dp_veh.x ) != 2 && abs( dp.y + dp_veh.y ) != 2 ) {
        // Not actually moving the vehicle, don't do the checks
        u.grab_point = -( dp + dp_veh );
        return false;
    } else if( ( dp.x == prev_grab.x || dp.y == prev_grab.y ) &&
               next_grab.x != 0 && next_grab.y != 0 ) {
        // Zig-zag (or semi-zig-zag) pull: player is diagonal to veh
        // and moves away from it, but not directly away
        dp_veh.x = ( dp.x == -dp_veh.x ) ? 0 : dp_veh.x;
        dp_veh.y = ( dp.y == -dp_veh.y ) ? 0 : dp_veh.y;

        next_grab = -dp_veh;
        zigzag = true;
    } else {
        // We are pulling the veh
        next_grab = -dp;
    }

    // Make sure the mass and pivot point are correct
    grabbed_vehicle->invalidate_mass();





    //---------




    // Check if player is pivoting around the grab point, i.e. not moving the vehicle.
    tripoint delta = dp - u.grab_point; // vector from grabbed point to player's new position
    bool is_pushing = ( delta.x == 0 && delta.y == 0 );
    bool is_pulling = ( abs(delta.x) == 2 || abs(delta.y) == 2 );
    if( !is_pushing && !is_pulling ) {
        // We are moving around the veh
        u.grab_point = -delta;
        return false;
    }
    // TODO pushing.
    if( is_pulling ) {

        // Handle pulling!
        // grabbed part relative to player = u.grab_point
        // Figure out grabbed part's destination relative to player.
        tileray movement_of_part_ray( dp.x - u.grab_point.x, dp.y - u.grab_point.y );
        movement_of_part_ray.advance();
        tripoint d_part( movement_of_part_ray.dx(), movement_of_part_ray.dy(), 0 );
        tripoint destination = u.grab_point + d_part;
        tripoint com_relative_to_vehicle;
        grabbed_vehicle->rotated_center_of_mass( com_relative_to_vehicle.x, com_relative_to_vehicle.y );
        // COM relative to player = u.grab_point + COM relative to vehicle - grabbed part relative to vehicle
        tripoint com = u.grab_point + ( com_relative_to_vehicle - grabbed_vehicle->parts[grabbed_part].precalc[0] );

        // theta is the angle between vectors a and b,
        // where a = from COM to part's destination
        // and b = from COM to grabbed part
        tripoint a = destination - com; // vector from COM to destination, relative to player
        // TODO Use doubles to store rotated part coordinates, get b (COM to part) from that
        point mount_coords = grabbed_vehicle->parts[grabbed_part].mount;
        int initial_face = grabbed_vehicle->face.dir();
        double sin_rotation = -sin( initial_face * M_PI / 180.0 );
        double cos_rotation = cos( initial_face * M_PI / 180.0 );
        double part_relative_to_vehicle_x = mount_coords.x * cos_rotation + mount_coords.y * sin_rotation;
        double part_relative_to_vehicle_y = mount_coords.y * cos_rotation - mount_coords.x * sin_rotation;
        double bx = part_relative_to_vehicle_x - com_relative_to_vehicle.x;
        double by = part_relative_to_vehicle_y - com_relative_to_vehicle.y;
        add_msg( m_warning, "========" );
        add_msg( m_warning, "rot:%d deg, com:(%d,%d), mount:(%d,%d), rotated:(%.2f,%.2f), b:(%.2f,%.2f)",
                initial_face,
                com_relative_to_vehicle.x, com_relative_to_vehicle.y,
                mount_coords.x, mount_coords.y,
                part_relative_to_vehicle_x, part_relative_to_vehicle_y,
                bx, by );
        tripoint b = u.grab_point - com; // vector from COM to grab
        double theta = 0.0;
        if( !( a.x == 0 && a.y == 0 ) && !( b.x == 0 && b.y == 0 ) ) {
            // Not dragging COM, or dragging to COM, so atan2 works
            theta = atan2( a.y, a.x ) - atan2( b.y, b.x );
        } // Otherwise theta is 0 since there's no rotation anyways when you drag from/towards COM

        // find how far the COM has to move. negative = inwards
        double r = hypot( b.y, b.x ); // distance between COM and grabbed part
        double ds = hypot( a.y, a.x ) - r;

        // parallel acceleration a = Fcosθ/m
        // perpendicular acceleration α = τ/I = rFsinθ/I
        // find dt/ds = α/a = (perpendicular/parallel) = (rFsinθ/I)/(Fcosθ/m) = (rm/I)tanθ
        // multiply by ds to get dt.

        double dt = ( r * grabbed_vehicle->total_mass() / grabbed_vehicle->moment_of_inertia() ) * tan( theta ) * std::abs( ds ); // (rm/I)tanθ * |ds|

add_msg( m_warning, "===" );
add_msg( m_warning, "com:(%d,%d), a(F):(%d,%d), b(r):(%d,%d), theta=%.1f deg", com.x, com.y, a.x, a.y, b.x, b.y, theta * 180.0 / M_PI );
//add_msg( m_warning, "r:%.2f, ds:%.2f, dt:%.1f deg", r, ds, dt * 180.0 / M_PI );

            // Turn vehicle about COM
        grabbed_vehicle->turn( dt * 180.0 / M_PI );
        grabbed_vehicle->face = grabbed_vehicle->turn_dir;
//add_msg( m_warning, "face: initial=%d deg, final=%d, delta=%d deg",
//        initial_face, grabbed_vehicle->face.dir(), grabbed_vehicle->face.dir() - initial_face );
        grabbed_vehicle->precalc_mounts( 1, grabbed_vehicle->face.dir(), grabbed_vehicle->pivot_point() );

        // Figure out where the grabbed part is, and move it to the destination
        tripoint vehicle_position = grabbed_vehicle->global_pos3();
        tripoint new_grabbed_part_position = vehicle_position + grabbed_vehicle->parts[grabbed_part].precalc[1];
        tripoint delta_position = ( u.pos() + destination ) - new_grabbed_part_position;
        m.displace_vehicle( vehicle_position, delta_position );

        // Shift the grab point to the part's position
        tripoint old = u.grab_point;
        u.grab_point = grabbed_vehicle->global_part_pos3( grabbed_part ) - ( u.pos() + dp );
//add_msg( m_warning, "u+dp:(%d,%d)+(%d,%d), part:(%d,%d). old grab:(%d,%d), new grab:(%d,%d)",
//        u.pos().x, u.pos().y, dp.x, dp.y, grabbed_vehicle->global_part_pos3( grabbed_part ).x, grabbed_vehicle->global_part_pos3( grabbed_part ).y,
//        old.x, old.y, u.grab_point.x, u.grab_point.y );
    }

    return false;

}
//*/