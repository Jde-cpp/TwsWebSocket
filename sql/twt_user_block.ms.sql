CREATE PROCEDURE twt_user_block( @id bigint )
as
	declare @existing bigint;
	set @existing=(select id from twt_users where id=@id);
	if( @existing is null )
		insert into twt_users(id,screen_name,blocked) values( @id, cast(@id as char(50)) , 1 );
	else
		update twt_users set blocked=1 where id=@id;
