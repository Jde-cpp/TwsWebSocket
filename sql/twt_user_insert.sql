CREATE PROCEDURE twt_user_insert( _id bigint unsigned, _screen_name varchar(15), _profile_image varchar(512) )
begin
	declare _existing varchar(15);
	select screen_name into _existing from twt_handles where id=_id;
	if( _existing is null ) then
		insert into twt_handles(id,screen_name,profile_image,blocked) values( _id, _screen_name, _profile_image, 0 );
	elseif( _existing=_screen_name ) then
		update twt_handles set profile_image=_profile_image where id=_id;
	else
		update twt_handles set screen_name=_screen_name, profile_image=_profile_image where id=_id;
	end if;
end