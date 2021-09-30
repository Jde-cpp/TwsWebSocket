CREATE PROCEDURE twt_user_block( _id bigint unsigned )
begin
	declare _existing bigint unsigned;
	select id into _existing from twt_handles where id=_id;
	if( _existing is null ) then
		insert into twt_handles(id,screen_name,blocked) values( _id, cast(_id as char(50)) , 1 );
	else
		update twt_handles set blocked=1 where id=_id;
	end if;
end