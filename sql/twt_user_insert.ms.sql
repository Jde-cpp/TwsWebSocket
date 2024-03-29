CREATE PROCEDURE twt_user_insert( @id bigint, @screen_name varchar(15), @profile_image varchar(512) )
as
	declare @existing varchar(15);
	set @existing=(select screen_name from twt_handles where id=@id);
	if( @existing is null )
		insert into twt_handles(id,screen_name,profile_image,blocked) values( @id, @screen_name, @profile_image, 0 );
	else if( @existing=@screen_name )
		update twt_handles set profile_image=@profile_image where id=@id;
	else
		update twt_handles set screen_name=@screen_name, profile_image=@profile_image where id=@id;
