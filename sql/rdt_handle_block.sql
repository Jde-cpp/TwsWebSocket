CREATE PROCEDURE rdt_handle_block( _name varchar(20) )
begin
	declare _existing int unsigned;
	select id into _existing from rdt_handles where name=_name;
	if( _existing is null ) then
		insert into rdt_handles(name,blocked) values( _name, 1 );
	else
		update rdt_handles set blocked=1 where id=_existing;
	end if;
end