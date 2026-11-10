.class public final Lcom/dbdm/switcher/ProfileStore;
.super Ljava/lang/Object;
.source "ProfileStore.kt"


# annotations
.annotation system Ldalvik/annotation/MemberClasses;
    value = {
        Lcom/dbdm/switcher/ProfileStore$Companion;
    }
.end annotation

.annotation runtime Lkotlin/Metadata;
    d1 = {
        "\u0000J\n\u0002\u0018\u0002\n\u0002\u0010\u0000\n\u0000\n\u0002\u0018\u0002\n\u0002\u0008\u0002\n\u0002\u0018\u0002\n\u0000\n\u0002\u0018\u0002\n\u0000\n\u0002\u0018\u0002\n\u0002\u0008\u0002\n\u0002\u0010\u0002\n\u0000\n\u0002\u0018\u0002\n\u0000\n\u0002\u0010!\n\u0002\u0008\u0002\n\u0002\u0010\u000e\n\u0002\u0008\u0002\n\u0002\u0010 \n\u0002\u0008\u0002\u0018\u0000 \u00182\u00020\u0001:\u0001\u0018B\r\u0012\u0006\u0010\u0002\u001a\u00020\u0003\u00a2\u0006\u0002\u0010\u0004J\u000e\u0010\u000c\u001a\u00020\r2\u0006\u0010\u000e\u001a\u00020\u000fJ\u000c\u0010\u0010\u001a\u0008\u0012\u0004\u0012\u00020\u000f0\u0011J\u000e\u0010\u0012\u001a\u00020\r2\u0006\u0010\u0013\u001a\u00020\u0014J\u0014\u0010\u0015\u001a\u00020\r2\u000c\u0010\u0016\u001a\u0008\u0012\u0004\u0012\u00020\u000f0\u0017R\u000e\u0010\u0005\u001a\u00020\u0006X\u0082\u0004\u00a2\u0006\u0002\n\u0000R\u000e\u0010\u0007\u001a\u00020\u0008X\u0082\u0004\u00a2\u0006\u0002\n\u0000R\u0016\u0010\t\u001a\n \u000b*\u0004\u0018\u00010\n0\nX\u0082\u0004\u00a2\u0006\u0002\n\u0000\u00a8\u0006\u0019"
    }
    d2 = {
        "Lcom/dbdm/switcher/ProfileStore;",
        "",
        "context",
        "Landroid/content/Context;",
        "(Landroid/content/Context;)V",
        "file",
        "Ljava/io/File;",
        "gson",
        "Lcom/google/gson/Gson;",
        "listType",
        "Ljava/lang/reflect/Type;",
        "kotlin.jvm.PlatformType",
        "add",
        "",
        "profile",
        "Lcom/dbdm/switcher/AccountProfile;",
        "load",
        "",
        "remove",
        "id",
        "",
        "save",
        "profiles",
        "",
        "Companion",
        "app_debug"
    }
    k = 0x1
    mv = {
        0x1,
        0x9,
        0x0
    }
    xi = 0x30
.end annotation


# static fields
.field public static final Companion:Lcom/dbdm/switcher/ProfileStore$Companion;


# instance fields
.field private final file:Ljava/io/File;

.field private final gson:Lcom/google/gson/Gson;

.field private final listType:Ljava/lang/reflect/Type;


# direct methods
.method static constructor <clinit>()V
    .locals 2

    new-instance v0, Lcom/dbdm/switcher/ProfileStore$Companion;

    const/4 v1, 0x0

    invoke-direct {v0, v1}, Lcom/dbdm/switcher/ProfileStore$Companion;-><init>(Lkotlin/jvm/internal/DefaultConstructorMarker;)V

    sput-object v0, Lcom/dbdm/switcher/ProfileStore;->Companion:Lcom/dbdm/switcher/ProfileStore$Companion;

    return-void
.end method

.method public constructor <init>(Landroid/content/Context;)V
    .locals 3
    .param p1, "context"    # Landroid/content/Context;

    const-string v0, "context"

    invoke-static {p1, v0}, Lkotlin/jvm/internal/Intrinsics;->checkNotNullParameter(Ljava/lang/Object;Ljava/lang/String;)V

    .line 22
    invoke-direct {p0}, Ljava/lang/Object;-><init>()V

    .line 24
    new-instance v0, Ljava/io/File;

    invoke-virtual {p1}, Landroid/content/Context;->getFilesDir()Ljava/io/File;

    move-result-object v1

    const-string v2, "profiles.json"

    invoke-direct {v0, v1, v2}, Ljava/io/File;-><init>(Ljava/io/File;Ljava/lang/String;)V

    iput-object v0, p0, Lcom/dbdm/switcher/ProfileStore;->file:Ljava/io/File;

    .line 25
    new-instance v0, Lcom/google/gson/Gson;

    invoke-direct {v0}, Lcom/google/gson/Gson;-><init>()V

    iput-object v0, p0, Lcom/dbdm/switcher/ProfileStore;->gson:Lcom/google/gson/Gson;

    .line 26
    new-instance v0, Lcom/dbdm/switcher/ProfileStore$listType$1;

    invoke-direct {v0}, Lcom/dbdm/switcher/ProfileStore$listType$1;-><init>()V

    invoke-virtual {v0}, Lcom/dbdm/switcher/ProfileStore$listType$1;->getType()Ljava/lang/reflect/Type;

    move-result-object v0

    iput-object v0, p0, Lcom/dbdm/switcher/ProfileStore;->listType:Ljava/lang/reflect/Type;

    .line 22
    return-void
.end method


# virtual methods
.method public final add(Lcom/dbdm/switcher/AccountProfile;)V
    .locals 1
    .param p1, "profile"    # Lcom/dbdm/switcher/AccountProfile;

    const-string v0, "profile"

    invoke-static {p1, v0}, Lkotlin/jvm/internal/Intrinsics;->checkNotNullParameter(Ljava/lang/Object;Ljava/lang/String;)V

    .line 42
    invoke-virtual {p0}, Lcom/dbdm/switcher/ProfileStore;->load()Ljava/util/List;

    move-result-object v0

    .line 43
    .local v0, "list":Ljava/util/List;
    invoke-interface {v0, p1}, Ljava/util/List;->add(Ljava/lang/Object;)Z

    .line 44
    invoke-virtual {p0, v0}, Lcom/dbdm/switcher/ProfileStore;->save(Ljava/util/List;)V

    .line 45
    return-void
.end method

.method public final load()Ljava/util/List;
    .locals 4
    .annotation system Ldalvik/annotation/Signature;
        value = {
            "()",
            "Ljava/util/List<",
            "Lcom/dbdm/switcher/AccountProfile;",
            ">;"
        }
    .end annotation

    .line 29
    iget-object v0, p0, Lcom/dbdm/switcher/ProfileStore;->file:Ljava/io/File;

    invoke-virtual {v0}, Ljava/io/File;->exists()Z

    move-result v0

    if-nez v0, :cond_0

    new-instance v0, Ljava/util/ArrayList;

    invoke-direct {v0}, Ljava/util/ArrayList;-><init>()V

    check-cast v0, Ljava/util/List;

    return-object v0

    .line 30
    :cond_0
    nop

    .line 31
    :try_start_0
    iget-object v0, p0, Lcom/dbdm/switcher/ProfileStore;->gson:Lcom/google/gson/Gson;

    iget-object v1, p0, Lcom/dbdm/switcher/ProfileStore;->file:Ljava/io/File;

    const/4 v2, 0x1

    const/4 v3, 0x0

    invoke-static {v1, v3, v2, v3}, Lkotlin/io/FilesKt;->readText$default(Ljava/io/File;Ljava/nio/charset/Charset;ILjava/lang/Object;)Ljava/lang/String;

    move-result-object v1

    iget-object v2, p0, Lcom/dbdm/switcher/ProfileStore;->listType:Ljava/lang/reflect/Type;

    invoke-virtual {v0, v1, v2}, Lcom/google/gson/Gson;->fromJson(Ljava/lang/String;Ljava/lang/reflect/Type;)Ljava/lang/Object;

    move-result-object v0

    check-cast v0, Ljava/util/List;

    if-nez v0, :cond_1

    new-instance v0, Ljava/util/ArrayList;

    invoke-direct {v0}, Ljava/util/ArrayList;-><init>()V

    check-cast v0, Ljava/util/List;
    :try_end_0
    .catch Ljava/lang/Exception; {:try_start_0 .. :try_end_0} :catch_0

    goto :goto_0

    .line 32
    :catch_0
    move-exception v0

    .line 33
    .local v0, "_":Ljava/lang/Exception;
    new-instance v1, Ljava/util/ArrayList;

    invoke-direct {v1}, Ljava/util/ArrayList;-><init>()V

    check-cast v1, Ljava/util/List;

    move-object v0, v1

    .line 30
    .end local v0    # "_":Ljava/lang/Exception;
    :cond_1
    :goto_0
    return-object v0
.end method

.method public final remove(Ljava/lang/String;)V
    .locals 2
    .param p1, "id"    # Ljava/lang/String;

    const-string v0, "id"

    invoke-static {p1, v0}, Lkotlin/jvm/internal/Intrinsics;->checkNotNullParameter(Ljava/lang/Object;Ljava/lang/String;)V

    .line 48
    invoke-virtual {p0}, Lcom/dbdm/switcher/ProfileStore;->load()Ljava/util/List;

    move-result-object v0

    .line 49
    .local v0, "list":Ljava/util/List;
    new-instance v1, Lcom/dbdm/switcher/ProfileStore$remove$1;

    invoke-direct {v1, p1}, Lcom/dbdm/switcher/ProfileStore$remove$1;-><init>(Ljava/lang/String;)V

    check-cast v1, Lkotlin/jvm/functions/Function1;

    invoke-static {v0, v1}, Lkotlin/collections/CollectionsKt;->removeAll(Ljava/util/List;Lkotlin/jvm/functions/Function1;)Z

    .line 50
    invoke-virtual {p0, v0}, Lcom/dbdm/switcher/ProfileStore;->save(Ljava/util/List;)V

    .line 51
    return-void
.end method

.method public final save(Ljava/util/List;)V
    .locals 4
    .param p1, "profiles"    # Ljava/util/List;
    .annotation system Ldalvik/annotation/Signature;
        value = {
            "(",
            "Ljava/util/List<",
            "Lcom/dbdm/switcher/AccountProfile;",
            ">;)V"
        }
    .end annotation

    const-string v0, "profiles"

    invoke-static {p1, v0}, Lkotlin/jvm/internal/Intrinsics;->checkNotNullParameter(Ljava/lang/Object;Ljava/lang/String;)V

    .line 38
    iget-object v0, p0, Lcom/dbdm/switcher/ProfileStore;->file:Ljava/io/File;

    iget-object v1, p0, Lcom/dbdm/switcher/ProfileStore;->gson:Lcom/google/gson/Gson;

    invoke-virtual {v1, p1}, Lcom/google/gson/Gson;->toJson(Ljava/lang/Object;)Ljava/lang/String;

    move-result-object v1

    const-string v2, "toJson(...)"

    invoke-static {v1, v2}, Lkotlin/jvm/internal/Intrinsics;->checkNotNullExpressionValue(Ljava/lang/Object;Ljava/lang/String;)V

    const/4 v2, 0x0

    const/4 v3, 0x2

    invoke-static {v0, v1, v2, v3, v2}, Lkotlin/io/FilesKt;->writeText$default(Ljava/io/File;Ljava/lang/String;Ljava/nio/charset/Charset;ILjava/lang/Object;)V

    .line 39
    return-void
.end method
